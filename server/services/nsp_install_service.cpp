#include "nsp_install_service.hpp"
#include "led_notif.hpp"
#include <switch.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <vector>

namespace services {

static constexpr size_t kBufSize = 65536;
static constexpr size_t kMaxFiles = 256;
static char s_buf[kBufSize];

struct Pfs0Header {
  uint32_t magic;
  uint32_t num_files;
  uint32_t str_table_size;
  uint32_t reserved;
};

struct Pfs0Entry {
  uint64_t offset;
  uint64_t size;
  uint32_t name_offset;
  uint32_t reserved;
};

struct ContentMetaHeader {
  uint16_t extended_header_size;
  uint16_t content_count;
  uint16_t content_meta_count;
  uint8_t  attributes;
  uint8_t  storage_id;
} __attribute__((packed));

struct ContentInfo {
  uint8_t  content_id[16];
  uint32_t size_low;
  uint8_t  size_high;
  uint8_t  attr;
  uint8_t  content_type;
  uint8_t  id_offset;
} __attribute__((packed));

static bool RecvExact(char* dst, size_t n,
                      std::function<ssize_t(char*, size_t)> on_recv) {
  size_t done = 0;
  while (done < n) {
    ssize_t r = on_recv(dst + done, n - done);
    if (r <= 0) return false;
    done += r;
  }
  return true;
}

static bool DrainBytes(size_t n, std::function<ssize_t(char*, size_t)> on_recv) {
  while (n > 0) {
    size_t to_read = n < kBufSize ? n : kBufSize;
    ssize_t r = on_recv(s_buf, to_read);
    if (r <= 0) return false;
    n -= static_cast<size_t>(r);
  }
  return true;
}

static bool IsCnmtNca(const char* name) {
  size_t len = strlen(name);
  const char* suffix = ".cnmt.nca";
  size_t slen = strlen(suffix);
  return len >= slen && strcmp(name + len - slen, suffix) == 0;
}

static bool IsNca(const char* name) {
  size_t len = strlen(name);
  const char* suffix = ".nca";
  size_t slen = strlen(suffix);
  return len >= slen && strcmp(name + len - slen, suffix) == 0 && !IsCnmtNca(name);
}

static bool ParseContentId(const char* name, NcmContentId* out) {
  if (strlen(name) < 32) return false;
  memset(out, 0, sizeof(*out));
  for (int i = 0; i < 16; i++) {
    char byte_str[3] = {name[i * 2], name[i * 2 + 1], '\0'};
    char* endp;
    unsigned long val = strtoul(byte_str, &endp, 16);
    if (endp != byte_str + 2) return false;
    out->c[i] = static_cast<uint8_t>(val);
  }
  return true;
}

static bool WriteNcaToPlaceholder(NcmContentStorage* cs,
                                   const NcmPlaceHolderId& ph,
                                   uint64_t size,
                                   std::function<ssize_t(char*, size_t)> on_recv) {
  uint64_t offset = 0;
  uint64_t remaining = size;
  while (remaining > 0) {
    size_t to_recv = remaining < kBufSize ? static_cast<size_t>(remaining) : kBufSize;
    ssize_t r = on_recv(s_buf, to_recv);
    if (r <= 0) return false;
    if (R_FAILED(ncmContentStorageWritePlaceHolder(cs, &ph, offset, s_buf,
                                                    static_cast<size_t>(r))))
      return false;
    offset += static_cast<uint64_t>(r);
    remaining -= static_cast<uint64_t>(r);
  }
  return true;
}

// Reads raw CNMT bytes from a mounted ContentMeta filesystem at the given path.
static bool ReadCnmtFromPath(const char* nca_path, std::vector<uint8_t>* out) {
  FsFileSystem fs{};
  if (R_FAILED(fsOpenFileSystemWithId(&fs, 0, FsFileSystemType_ContentMeta,
                                       nca_path, FsContentAttributes_None)))
    return false;

  if (fsdevMountDevice("nxmcpcnmt", fs) < 0) {
    fsFsClose(&fs);
    return false;
  }

  bool ok = false;
  DIR* dir = opendir("nxmcpcnmt:/");
  if (dir) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
      size_t nlen = strlen(ent->d_name);
      const char* suffix = ".cnmt";
      size_t slen = strlen(suffix);
      if (nlen >= slen && strcmp(ent->d_name + nlen - slen, suffix) == 0) {
        std::string cnmt_path = std::string("nxmcpcnmt:/") + ent->d_name;
        FILE* fp = fopen(cnmt_path.c_str(), "rb");
        if (fp) {
          fseek(fp, 0, SEEK_END);
          long sz = ftell(fp);
          fseek(fp, 0, SEEK_SET);
          if (sz > 0) {
            out->resize(static_cast<size_t>(sz));
            ok = fread(out->data(), 1, static_cast<size_t>(sz), fp)
                 == static_cast<size_t>(sz);
          }
          fclose(fp);
        }
        break;
      }
    }
    closedir(dir);
  }

  fsdevUnmountDevice("nxmcpcnmt");
  return ok;
}

static bool ReadCnmtFromPlaceholder(NcmContentStorage* cs,
                                     const NcmPlaceHolderId& ph,
                                     std::vector<uint8_t>* out) {
  char path[FS_MAX_PATH] = {};
  if (R_FAILED(ncmContentStorageGetPlaceHolderPath(cs, path, sizeof(path), &ph)))
    return false;
  return ReadCnmtFromPath(path, out);
}

static bool ReadCnmtFromInstalled(NcmContentStorage* cs,
                                   const NcmContentId& id,
                                   std::vector<uint8_t>* out) {
  char path[FS_MAX_PATH] = {};
  if (R_FAILED(ncmContentStorageGetPath(cs, path, sizeof(path), &id)))
    return false;
  return ReadCnmtFromPath(path, out);
}

static bool BuildContentMetaBlob(const std::vector<uint8_t>& cnmt,
                                  const NcmContentId& meta_id,
                                  uint64_t meta_size,
                                  std::vector<uint8_t>* blob_out,
                                  NcmContentMetaKey* key_out,
                                  uint64_t* application_id_out) {
  if (cnmt.size() < 0x20) return false;

  uint64_t title_id;
  uint32_t version;
  uint8_t  type;
  uint16_t ext_hdr_size;
  uint16_t content_count;
  uint16_t content_meta_count;
  uint8_t  attributes;

  memcpy(&title_id,           cnmt.data() + 0x00, 8);
  memcpy(&version,            cnmt.data() + 0x08, 4);
  type              = cnmt[0x0C];
  memcpy(&ext_hdr_size,       cnmt.data() + 0x0E, 2);
  memcpy(&content_count,      cnmt.data() + 0x10, 2);
  memcpy(&content_meta_count, cnmt.data() + 0x12, 2);
  attributes        = cnmt[0x14];

  *application_id_out = title_id;
  if ((type == NcmContentMetaType_Patch || type == NcmContentMetaType_AddOnContent)
      && ext_hdr_size >= 8 && cnmt.size() >= 0x28) {
    memcpy(application_id_out, cnmt.data() + 0x20, 8);
  }

  key_out->id           = title_id;
  key_out->version      = version;
  key_out->type         = static_cast<NcmContentMetaType>(type);
  key_out->install_type = NcmContentInstallType_Full;

  size_t records_offset = 0x20 + ext_hdr_size;
  if (cnmt.size() < records_offset + content_count * 0x38u) return false;

  std::vector<ContentInfo> infos;
  for (uint16_t i = 0; i < content_count; i++) {
    const uint8_t* rec = cnmt.data() + records_offset + i * 0x38;
    uint8_t c_type = rec[0x36];
    if (c_type == NcmContentType_DeltaFragment) continue;

    ContentInfo ci{};
    memcpy(ci.content_id, rec + 0x20, 16);
    uint64_t sz = 0;
    for (int b = 0; b < 5; b++) sz |= (static_cast<uint64_t>(rec[0x30 + b]) << (8 * b));
    ci.size_low    = static_cast<uint32_t>(sz & 0xFFFFFFFF);
    ci.size_high   = static_cast<uint8_t>((sz >> 32) & 0xFF);
    ci.attr        = rec[0x35];
    ci.content_type = c_type;
    ci.id_offset   = rec[0x37];
    infos.push_back(ci);
  }

  // Append the meta NCA itself as a content entry (type 5 = Meta)
  ContentInfo meta_ci{};
  memcpy(meta_ci.content_id, &meta_id, 16);
  meta_ci.size_low    = static_cast<uint32_t>(meta_size & 0xFFFFFFFF);
  meta_ci.size_high   = static_cast<uint8_t>((meta_size >> 32) & 0xFF);
  meta_ci.content_type = NcmContentType_Meta;
  infos.push_back(meta_ci);

  ContentMetaHeader hdr{};
  hdr.extended_header_size = ext_hdr_size;
  hdr.content_count        = static_cast<uint16_t>(infos.size());
  hdr.content_meta_count   = content_meta_count;
  hdr.attributes           = attributes;
  hdr.storage_id           = 0;

  size_t blob_size = sizeof(ContentMetaHeader) + ext_hdr_size
                   + infos.size() * sizeof(ContentInfo);
  blob_out->resize(blob_size);
  uint8_t* p = blob_out->data();
  memcpy(p, &hdr, sizeof(hdr));
  p += sizeof(hdr);
  if (ext_hdr_size > 0) {
    memcpy(p, cnmt.data() + 0x20, ext_hdr_size);
    p += ext_hdr_size;
  }
  for (auto& ci : infos) {
    memcpy(p, &ci, sizeof(ci));
    p += sizeof(ci);
  }
  return true;
}

static bool PushNsRecord(uint64_t application_id,
                          const NcmContentMetaKey& key,
                          uint8_t storage_id) {
  if (R_FAILED(nsInitialize())) return false;

  Service mgr{};
  if (R_FAILED(nsGetApplicationManagerInterface(&mgr))) {
    nsExit();
    return false;
  }

  struct ContentStorageRecord {
    NcmContentMetaKey key;
    uint8_t storage_id;
    uint8_t padding[7];
  };

  // Remove any stale records for this application before pushing the new one
  {
    const uint64_t del_in = application_id;
    serviceDispatchIn(&mgr, 27, del_in);
  }

  ContentStorageRecord record{};
  record.key        = key;
  record.storage_id = storage_id;

  const struct {
    uint8_t event;
    uint8_t pad[7];
    uint64_t tid;
  } push_in = {0x3, {}, application_id};

  Result rc = serviceDispatchIn(&mgr, 16, push_in,
      .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_In},
      .buffers      = {{&record, sizeof(record)}});

  serviceClose(&mgr);

  if (R_SUCCEEDED(rc) && hosversionAtLeast(6, 0, 0)) {
    if (key.type == NcmContentMetaType_Application
        || key.type == NcmContentMetaType_Patch) {
      if (R_SUCCEEDED(avmInitialize())) {
        avmPushLaunchVersion(application_id, key.version);
        avmExit();
      }
    }
  }

  nsExit();
  return R_SUCCEEDED(rc);
}

NspInstallStatus NspInstallService::Install(
    size_t /* total_size */,
    uint8_t storage_id,
    std::function<ssize_t(char*, size_t)> on_recv) {
  led::LedGuard guard;

  if (R_FAILED(ncmInitialize()))
    return NspInstallStatus::kNcaInstallFailed;
  struct NcmGuard { ~NcmGuard() { ncmExit(); } } ncm_guard;

  // --- Step 1: PFS0 header ---
  Pfs0Header hdr{};
  if (!RecvExact(reinterpret_cast<char*>(&hdr), sizeof(hdr), on_recv))
    return NspInstallStatus::kNetworkError;
  if (hdr.magic != 0x30534650u)
    return NspInstallStatus::kInvalidPfs0;
  if (hdr.num_files > kMaxFiles)
    return NspInstallStatus::kTooManyFiles;

  // --- Step 2: Entry table ---
  std::vector<Pfs0Entry> entries(hdr.num_files);
  if (!RecvExact(reinterpret_cast<char*>(entries.data()),
                 hdr.num_files * sizeof(Pfs0Entry), on_recv))
    return NspInstallStatus::kNetworkError;

  // --- Step 3: String table ---
  std::vector<char> str_table(hdr.str_table_size + 1, '\0');
  if (hdr.str_table_size > 0
      && !RecvExact(str_table.data(), hdr.str_table_size, on_recv))
    return NspInstallStatus::kNetworkError;

  // Entry offsets are relative to the first byte after this header region.
  // Track our position in that data section to drain any alignment gaps.
  uint64_t stream_pos = 0;

  // --- Step 4: Open NCM content storage ---
  NcmContentStorage cs{};
  NcmStorageId ncm_storage = static_cast<NcmStorageId>(storage_id);
  if (R_FAILED(ncmOpenContentStorage(&cs, ncm_storage)))
    return NspInstallStatus::kNcaInstallFailed;

  // --- Step 5: Stream the data section, installing each NCA ---
  NcmContentId     cnmt_id{};
  uint64_t         cnmt_size = 0;
  NcmPlaceHolderId cnmt_ph{};
  bool             cnmt_already_installed = false;
  bool             found_cnmt = false;
  NspInstallStatus result = NspInstallStatus::kOk;

  for (uint32_t i = 0; i < hdr.num_files && result == NspInstallStatus::kOk; i++) {
    const char* name = str_table.data() + entries[i].name_offset;
    uint64_t entry_offset = entries[i].offset;
    uint64_t entry_size   = entries[i].size;

    if (entry_offset > stream_pos) {
      if (!DrainBytes(static_cast<size_t>(entry_offset - stream_pos), on_recv)) {
        result = NspInstallStatus::kNetworkError;
        break;
      }
      stream_pos = entry_offset;
    }

    if (IsCnmtNca(name)) {
      if (!ParseContentId(name, &cnmt_id)) {
        result = NspInstallStatus::kNcaInstallFailed;
        break;
      }
      cnmt_size = entry_size;
      found_cnmt = true;

      bool already = false;
      ncmContentStorageHas(&cs, &already, &cnmt_id);

      if (already) {
        // CNMT NCA is installed — drain wire bytes and read from existing content.
        if (!DrainBytes(static_cast<size_t>(entry_size), on_recv)) {
          result = NspInstallStatus::kNetworkError;
          break;
        }
        cnmt_already_installed = true;
      } else {
        ncmContentStorageGeneratePlaceHolderId(&cs, &cnmt_ph);
        if (R_FAILED(ncmContentStorageCreatePlaceHolder(&cs, &cnmt_id, &cnmt_ph,
                                                         entry_size))) {
          result = NspInstallStatus::kNcaInstallFailed;
          break;
        }
        if (!WriteNcaToPlaceholder(&cs, cnmt_ph, entry_size, on_recv)) {
          result = NspInstallStatus::kNetworkError;
          break;
        }
        ncmContentStorageFlushPlaceHolder(&cs);
      }

    } else if (IsNca(name)) {
      NcmContentId nca_id{};
      if (!ParseContentId(name, &nca_id)) {
        result = NspInstallStatus::kNcaInstallFailed;
        break;
      }

      bool already = false;
      ncmContentStorageHas(&cs, &already, &nca_id);

      if (already) {
        if (!DrainBytes(static_cast<size_t>(entry_size), on_recv)) {
          result = NspInstallStatus::kNetworkError;
          break;
        }
      } else {
        NcmPlaceHolderId ph{};
        ncmContentStorageGeneratePlaceHolderId(&cs, &ph);
        if (R_FAILED(ncmContentStorageCreatePlaceHolder(&cs, &nca_id, &ph,
                                                         entry_size))) {
          result = NspInstallStatus::kNcaInstallFailed;
          break;
        }
        if (!WriteNcaToPlaceholder(&cs, ph, entry_size, on_recv)) {
          result = NspInstallStatus::kNetworkError;
          break;
        }
        ncmContentStorageFlushPlaceHolder(&cs);
        if (R_FAILED(ncmContentStorageRegister(&cs, &nca_id, &ph))) {
          result = NspInstallStatus::kNcaInstallFailed;
          break;
        }
      }

    } else {
      // .tik, .cert, and other non-NCA files — not installed, just consumed
      if (!DrainBytes(static_cast<size_t>(entry_size), on_recv)) {
        result = NspInstallStatus::kNetworkError;
        break;
      }
    }

    stream_pos += entry_size;
  }

  if (result != NspInstallStatus::kOk) {
    ncmContentStorageClose(&cs);
    return result;
  }

  if (!found_cnmt) {
    ncmContentStorageClose(&cs);
    return NspInstallStatus::kCnmtMissing;
  }

  // --- Step 6: Read raw CNMT bytes and register the CNMT NCA ---
  std::vector<uint8_t> cnmt_bytes;
  if (cnmt_already_installed) {
    if (!ReadCnmtFromInstalled(&cs, cnmt_id, &cnmt_bytes)) {
      ncmContentStorageClose(&cs);
      return NspInstallStatus::kCnmtReadFailed;
    }
  } else {
    if (!ReadCnmtFromPlaceholder(&cs, cnmt_ph, &cnmt_bytes)) {
      ncmContentStorageClose(&cs);
      return NspInstallStatus::kCnmtReadFailed;
    }
    if (R_FAILED(ncmContentStorageRegister(&cs, &cnmt_id, &cnmt_ph))) {
      ncmContentStorageClose(&cs);
      return NspInstallStatus::kNcaInstallFailed;
    }
  }

  ncmContentStorageClose(&cs);

  // --- Step 7: Build and commit content meta record ---
  std::vector<uint8_t> meta_blob;
  NcmContentMetaKey   meta_key{};
  uint64_t            application_id = 0;
  if (!BuildContentMetaBlob(cnmt_bytes, cnmt_id, cnmt_size,
                             &meta_blob, &meta_key, &application_id))
    return NspInstallStatus::kCnmtReadFailed;

  NcmContentMetaDatabase db{};
  if (R_FAILED(ncmOpenContentMetaDatabase(&db, ncm_storage)))
    return NspInstallStatus::kCnmtRegisterFailed;

  Result rc = ncmContentMetaDatabaseSet(&db, &meta_key,
                                         meta_blob.data(),
                                         static_cast<uint64_t>(meta_blob.size()));
  if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseCommit(&db);
  ncmContentMetaDatabaseClose(&db);

  if (R_FAILED(rc))
    return NspInstallStatus::kCnmtRegisterFailed;

  // --- Step 8: Push NS application record so the title appears in HOME menu ---
  if (!PushNsRecord(application_id, meta_key, storage_id))
    return NspInstallStatus::kRecordFailed;

  return NspInstallStatus::kOk;
}

}  // namespace services
