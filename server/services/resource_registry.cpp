#include "resource_registry.hpp"

namespace services {

ResourceRegistry& ResourceRegistry::Instance() {
  static ResourceRegistry instance;
  return instance;
}

void ResourceRegistry::Register(Resource resource) {
  resources_.emplace(resource.uri, std::move(resource));
}

const Resource* ResourceRegistry::Get(const std::string& uri) const {
  auto it = resources_.find(uri);
  return it != resources_.end() ? &it->second : nullptr;
}

std::vector<const Resource*> ResourceRegistry::All() const {
  std::vector<const Resource*> out;
  out.reserve(resources_.size());
  for (const auto& kv : resources_) out.push_back(&kv.second);
  return out;
}

ResourceRegistry::ResourceRegistry() {
  Register({
    "nxmcp://docs/file-transfer",
    "File Transfer",
    "HTTP endpoints for downloading, uploading, and installing files",
    "text/plain",
    "FILE TRANSFER — HTTP endpoints under /fs/\n"
    "\n"
    "All three endpoints stream in 64 KB chunks. Arbitrarily large files are supported.\n"
    "Prefer these over fs_ MCP tools for large files to avoid loading content into context.\n"
    "Spaces and brackets in sdmc paths must be URL-encoded (%20, %5B, %5D).\n"
    "\n"
    "Download — stream a file from the SD card to the local machine:\n"
    "  curl -s -o <local_path> \"http://<ip>:57331/fs/download?path=<sdmc_path>\"\n"
    "\n"
    "Upload — stream a file from the local machine to the SD card:\n"
    "  curl -s -X PUT \"http://<ip>:57331/fs/upload?path=<sdmc_path>\" --data-binary @<local_file>\n"
    "\n"
    "Install NSP — stream an NSP directly into NCM content storage (no full write to SD):\n"
    "  curl -s -X PUT \"http://<ip>:57331/fs/install/nsp?storage=sd\" \\\n"
    "    -H \"Content-Type: application/octet-stream\" --data-binary @<local.nsp>\n"
    "  storage=sd (default) or storage=nand\n"
    "  See nxmcp://docs/nsp-install for full details and limitations.\n"
  });

  Register({
    "nxmcp://docs/nsp-install",
    "NSP Install",
    "How the streaming NSP installer works and its limitations",
    "text/plain",
    "NSP INSTALL — PUT /fs/install/nsp\n"
    "\n"
    "Query params:\n"
    "  storage=sd    install to NcmStorageId_SdCard (default)\n"
    "  storage=nand  install to NcmStorageId_BuiltInUser\n"
    "\n"
    "Request: raw NSP bytes in body, Content-Length header required.\n"
    "Response: {\"success\":true} or {\"success\":false,\"error\":\"<reason>\"}\n"
    "\n"
    "Error values:\n"
    "  invalid_pfs0         Bad PFS0 magic — not a valid NSP\n"
    "  too_many_files       NSP contains more than 256 files\n"
    "  nca_install_failed   NCM operation failed (storage open, placeholder create/register)\n"
    "  cnmt_missing         No .cnmt.nca found in the NSP\n"
    "  cnmt_read_failed     System could not open the CNMT NCA as a ContentMeta filesystem.\n"
    "                       Usually means RightsId (title-key) encryption.\n"
    "  cnmt_register_failed NCM content meta database commit failed\n"
    "  record_failed        NS application record push failed (title may still be installed)\n"
    "  network_error        Socket read error mid-stream\n"
    "\n"
    "What the installer does:\n"
    "  1. Reads the PFS0 header, entry table, and string table (manifest is at the front\n"
    "     of every NSP so no seeking is needed).\n"
    "  2. Streams each NCA in order, writing directly to an NCM placeholder on the SD card.\n"
    "     Already-installed NCAs are drained and skipped.\n"
    "  3. After the .cnmt.nca is written, opens it via fsOpenFileSystemWithId with\n"
    "     FsFileSystemType_ContentMeta — the system handles NCA decryption, no manual\n"
    "     crypto is needed.\n"
    "  4. Constructs the content meta blob and calls ncmContentMetaDatabaseSet + commit.\n"
    "  5. Pushes an NS application record so the title appears in the HOME menu.\n"
    "\n"
    "Ticket (.tik) and cert files are skipped. Standard-crypto titles work on CFW\n"
    "without tickets. RightsId-encrypted NSPs (requiring a title key) will fail at\n"
    "cnmt_read_failed — use Tinfoil for those.\n"
  });

  Register({
    "nxmcp://docs/save-data",
    "Save Data",
    "Rules for mounting and editing game save data",
    "text/plain",
    "SAVE DATA — mount_save / unmount_save\n"
    "\n"
    "Rules:\n"
    "  - A save can only be mounted while the owning game is NOT running.\n"
    "    Mounting while the game is active will fail or corrupt save data.\n"
    "  - Always call unmount_save immediately after finishing work on a save.\n"
    "    Never leave a save mounted between unrelated tool calls.\n"
    "\n"
    "Mounted saves are accessible via:\n"
    "  fs_list_dir, fs_read_file, fs_write_file\n"
    "  /fs/download and /fs/upload HTTP endpoints\n"
    "  Use the mount name as the path prefix, e.g. \"save:/\" or \"save:/file.dat\".\n"
  });

  Register({
    "nxmcp://docs/process-model",
    "Process Model",
    "Horizon OS process pools, program IDs, and launching/killing processes",
    "text/plain",
    "PROCESS MODEL — Horizon OS\n"
    "\n"
    "Memory pools:\n"
    "  Application   Foreground game. Tagged [app] in pm_list_processes.\n"
    "  Applet        Album, overlays, and other applet-type processes.\n"
    "  System        Sysmodules and system services.\n"
    "\n"
    "Program IDs are 64-bit hex values.\n"
    "  Nintendo system titles:    0x0100000000000000 – 0x01FFFFFFFFFFFFFF\n"
    "  Homebrew sysmodules:       0x420000000000XXXX range (typical)\n"
    "  NX-MCP:                    0x0100000000057331\n"
    "\n"
    "Process tools:\n"
    "  pm_list_processes              List all running processes (PID, program ID, pool usage)\n"
    "  pm_launch <program_id>         Launch a program by program ID\n"
    "  pm_kill <pid|program_id>       Terminate a process\n"
    "  pm_get_process_info <pid>      Details for a specific process\n"
    "\n"
    "Sysmodules can be launched with pm_launch without rebooting on Atmosphère CFW.\n"
    "If a sysmodule was just installed (exefs.nsp copied to SD), pm_launch picks up\n"
    "the new binary immediately.\n"
  });

  Register({
    "nxmcp://docs/sysmodule-install",
    "Sysmodule Install",
    "How to install Atmosphere sysmodules and overlays to the SD card",
    "text/plain",
    "SYSMODULE INSTALL — Atmosphère layout\n"
    "\n"
    "Sysmodule directory: sdmc:/atmosphere/contents/<title_id>/\n"
    "  Title IDs in paths have NO 0x prefix.\n"
    "  exefs.nsp            The sysmodule binary\n"
    "  toolbox.json         Optional service registration metadata\n"
    "  flags/boot2.flag     If present, Atmosphère auto-launches this module at boot\n"
    "\n"
    "NX-MCP paths:\n"
    "  sdmc:/atmosphere/contents/0100000000057331/exefs.nsp\n"
    "  sdmc:/atmosphere/contents/0100000000057331/flags/boot2.flag\n"
    "\n"
    "To install a sysmodule from a zip file:\n"
    "  1. Inspect the zip to find the atmosphere/contents/<title_id>/ structure.\n"
    "  2. Create directories with fs_mkdir (parent must exist first).\n"
    "  3. Upload each file with /fs/upload.\n"
    "  4. Launch immediately with pm_launch <program_id> — no reboot needed on CFW.\n"
    "\n"
    "Tesla overlays:\n"
    "  .ovl files go in sdmc:/switch/.overlays/\n"
    "  nx-ovlloader (program ID 0x420000000007E51A) must be running.\n"
    "  Launch with pm_launch if not already running.\n"
    "  Open Tesla overlay menu: L + DPad-Down + R-stick click.\n"
    "\n"
    "NRO homebrew apps go in sdmc:/switch/\n"
  });
}

}  // namespace services
