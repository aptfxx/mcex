// mcex v1
// extracts minecraft bedrock worlds to somewhere you can actually find them


// i wrote a whole linux part of this because i usually write my code for the day and THEN comment on it (its my thing)
// and then i realised "oh yeah bedrock isnt on linux"
// i have no clue where i got my sources from, google ai overview being shitty yet again
// however im too lazy to remove it and it doesnt do anything being there so uhhhh
// i just wont publish the linux binary and we will all be fine and dandy


#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <chrono>
#include <iomanip>
#include <ctime>

namespace fs = std::filesystem;

#ifdef _WIN32
  #include <windows.h>
  #define PLATFORM_WINDOWS
#else
  #include <unistd.h>
  #include <pwd.h>
  #define PLATFORM_LINUX
#endif

namespace col {
  const std::string reset  = "\033[0m";
  const std::string cyan   = "\033[96m";
  const std::string yellow = "\033[93m";
  const std::string green  = "\033[92m";
  const std::string red    = "\033[91m";
  const std::string grey   = "\033[90m";
}

// same deal as before. try a bunch of things, give up and use /tmp if everything fails
fs::path getHomeDir() {
#ifdef PLATFORM_WINDOWS
  const char* home = std::getenv("USERPROFILE");
  if (home) return fs::path(home);
  return fs::path("C:/Users/User"); // sorry if your name isn't User. i really am 
#else
  const char* home = std::getenv("HOME");
  if (home) return fs::path(home);
  struct passwd* pw = getpwuid(getuid());
  if (pw) return fs::path(pw->pw_dir);
  return fs::path("/tmp"); // absolute last resort. we tried
#endif
}

// exports go here. relative to wherever mcex.exe lives
fs::path getExportsDir() {
  return fs::current_path() / "mcex" / "exports";
}

// logs go here. same deal
fs::path getLogsDir() {
  return fs::current_path() / "mcex" / "logs";
}

void clearScreen() {
  // yes this is bad. yes i'm doing it anyway. it's a TUI app, chill out
  // i did this shit in nmod too but i am really a bad developer because im not fixing it until i go viral like ddlc and i will
  // be burned at the stake for doing this by the public
#ifdef PLATFORM_WINDOWS
  system("cls");
#else
  system("clear");
#endif
}

void waitForEnter() {
  std::cout << "\nPress Enter to go back...";
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

std::string getInput(const std::string& prompt) {
  std::cout << prompt;
  std::string input;
  std::getline(std::cin, input);
  return input;
}

// trims whitespace. same as nmod, not going to pretend i rewrote it
std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\r\n");
  size_t end   = s.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  return s.substr(start, end - start + 1);
}

bool iequals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (int i = 0; i < (int)a.size(); i++)
    if (tolower(a[i]) != tolower(b[i])) return false;
  return true;
}

// copies a whole directory recursively. stolen from nmod with minor guilt
bool copyDir(const fs::path& src, const fs::path& dst) {
  try {
    fs::create_directories(dst);
    for (const auto& item : fs::recursive_directory_iterator(src)) {
      fs::path relative = fs::relative(item.path(), src);
      fs::path target   = dst / relative;
      if (item.is_directory()) fs::create_directories(target);
      else fs::copy_file(item.path(), target, fs::copy_options::overwrite_existing);
    }
    return true;
  } catch (const std::exception& e) {
    std::cerr << col::red << "Copy error: " << e.what() << col::reset << "\n";
    return false;
  }
}

std::string nowString() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm* tm_info = std::localtime(&t); // not thread safe but we're single threaded so whatever
  std::ostringstream ss;
  ss << std::put_time(tm_info, "%Y%m%d_%H%M%S");
  return ss.str();
}

void printBanner() {
// ok thus looks a liitle better than the nmod one which looked nothing like the word nmod
  std::cout << col::cyan;
  std::cout << "\n";
  std::cout << "    __  ________________  __\n";
  std::cout << "   /  |/  / ____/ ____/ |/ /\n";
  std::cout << "  / /|_/ / /   / __/  |   / \n";
  std::cout << " / /  / / /___/ /___ /   |  \n";
  std::cout << "/_/  /_/\\____/_____//_/|_|  \n";
  std::cout << col::reset;
  std::cout << "                                            -MCEX v1\n";
  std::cout << "\n";
}

std::string readLevelName(const fs::path& worldDir) {
  fs::path levelnameFile = worldDir / "levelname.txt";
  if (!fs::exists(levelnameFile)) return "(no levelname.txt)"; // it should always exist but here we are

  std::ifstream f(levelnameFile);
  if (!f.is_open()) return "(unreadable)";

  std::string name;
  std::getline(f, name); // just the first line. the rest is usually nothing
  return trim(name);
}

// a world has a levelname.txt and probably a level.dat. if it has neither it's probably not a world
bool looksLikeAWorld(const fs::path& dir) {
  if (!fs::is_directory(dir)) return false;
  // levelname.txt is the bare minimum. level.dat would be better but bedrock is inconsistent
  return fs::exists(dir / "levelname.txt") || fs::exists(dir / "level.dat");
}

struct WorldEntry {
  fs::path    path;       // full path to the world folder (the ugly hash name)
  std::string folderName; // just the ugly hash name
  std::string levelName;  // the human readable name from levelname.txt
};

// represents one microsoft account's world folder
// just a path and a display label so the user knows which account is which
struct UserAccount {
  fs::path    worldsDir;
  std::string accountId; 
};

// scans the worlds directory and returns everything that looks vaguely world-shaped
std::vector<WorldEntry> discoverWorlds(const fs::path& worldsDir, std::ostream& log) {
  std::vector<WorldEntry> worlds;

  log << "[scan] Looking in: " << worldsDir.string() << "\n";

  if (!fs::exists(worldsDir)) {
    log << "[error] Worlds directory does not exist: " << worldsDir.string() << "\n";
    return worlds; // empty. caller handles this
  }

  if (!fs::is_directory(worldsDir)) {
    log << "[error] Path exists but is not a directory: " << worldsDir.string() << "\n";
    return worlds;
  }

  int scanned = 0;
  int skipped = 0;

  for (const auto& item : fs::directory_iterator(worldsDir)) {
    scanned++;
    if (!item.is_directory()) {
      log << "[skip] Not a directory: " << item.path().filename().string() << "\n";
      skipped++;
      continue;
    }

    if (!looksLikeAWorld(item.path())) {
      log << "[skip] Doesn't look like a world (no levelname.txt or level.dat): "
          << item.path().filename().string() << "\n";
      skipped++;
      continue;
    }

    WorldEntry e;
    e.path       = item.path();
    e.folderName = item.path().filename().string();
    e.levelName  = readLevelName(item.path());

    log << "[found] Folder: " << e.folderName << " -> Level name: \"" << e.levelName << "\"\n";
    worlds.push_back(e);
  }

  log << "[scan] Done. Scanned " << scanned << " items, skipped " << skipped
      << ", found " << worlds.size() << " world(s).\n";

  // sort alphabetically by display name so the list is predictable
  std::sort(worlds.begin(), worlds.end(), [](const WorldEntry& a, const WorldEntry& b) {
    return a.levelName < b.levelName;
  });

  return worlds;
}

// scans AppData\Roaming\Minecraft Bedrock\Users\ and returns one entry per account subfolder
// that actually has a minecraftWorlds directory inside it
// returns empty if the launcher path doesn't exist at all
std::vector<UserAccount> discoverAccounts(std::ostream& log) {
  std::vector<UserAccount> accounts;

#ifdef PLATFORM_WINDOWS
  const char* appdata = std::getenv("APPDATA"); // APPDATA = AppData\Roaming. not Local. windows is fun.
  if (!appdata) {
    log << "[error] APPDATA environment variable not set. Can't find launcher path.\n";
    return accounts;
  }

  fs::path mcRoot = fs::path(appdata) / "Minecraft Bedrock" / "Users";
  log << "[info] Scanning for accounts in: " << mcRoot.string() << "\n";

  if (!fs::exists(mcRoot) || !fs::is_directory(mcRoot)) {
    log << "[info] Launcher Users folder not found: " << mcRoot.string() << "\n";
    return accounts; // empty, caller will offer legacy mode
  }

  for (const auto& item : fs::directory_iterator(mcRoot)) {
    if (!item.is_directory()) continue;
    if (iequals(item.path().filename().string(), "Shared")) {
      log << "[skip] Ignoring Shared folder (contains no worlds).\n";
      continue; // mojang puts a Shared folder in there. it has nothing. ignore it.
    }
    fs::path candidate = item.path() / "games" / "com.mojang" / "minecraftWorlds";
    log << "[info] Checking account: " << item.path().filename().string() << "\n";
    if (fs::exists(candidate)) {
      log << "[found] Account has worlds dir: " << candidate.string() << "\n";
      UserAccount a;
      a.worldsDir = candidate;
      a.accountId = item.path().filename().string();
      accounts.push_back(a);
    } else {
      log << "[skip] No minecraftWorlds in: " << item.path().string() << "\n";
    }
  }

#else
  // oh wait, linux doesnt have mincraft bedrock support. i literally only realised this idk where im getting my data from but i suppose im too lazy to get rid of it now
  // curse you extra bytes and kilobytes and megobytes
  fs::path linuxPath = getHomeDir() / ".var" / "app" / "io.mrarm.mcpelauncher" / "data"
                                    / "mcpelauncher" / "games" / "com.mojang" / "minecraftWorlds";
  log << "[info] Checking Linux flatpak path: " << linuxPath.string() << "\n";
  if (fs::exists(linuxPath)) {
    UserAccount a;
    a.worldsDir = linuxPath;
    a.accountId = "default";
    accounts.push_back(a);
  }
#endif

  return accounts;
}

// the legacy UWP store path (old microsoft store version, LocalAppData\Packages\...\minecraftWorlds)
// mojang moved away from this with the standalone launcher but some people still have it because theyre boomers
fs::path getLegacyWorldsDir(std::ostream& log) {
#ifdef PLATFORM_WINDOWS
  const char* localappdata = std::getenv("LOCALAPPDATA");
  if (localappdata) {
    fs::path p = fs::path(localappdata) / "Packages" / "Microsoft.MinecraftUWP_8wekyb3d8bbwe"
                                        / "LocalState" / "games" / "com.mojang" / "minecraftWorlds";
    log << "[legacy] Checking UWP path: " << p.string() << "\n";
    return p;
  }
  log << "[legacy] LOCALAPPDATA not set. Can't build legacy path.\n";
  return fs::path(); // empty. caller checks .empty()
#else
  log << "[legacy] Legacy UWP mode only applies on Windows.\n";
  return fs::path();
#endif
}

// sanitises a world name so it can be used as a folder name without exploding the filesystem
// minecraft lets you name worlds with slashes, colons, question marks, etc. because why not
std::string safeFolderName(const std::string& name, const std::string& fallback) {
  std::string s = name;
  for (char& c : s) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<'  || c == '>' || c == '|') {
      c = '_'; // boring but functional
    }
  }
  if (trim(s).empty()) return fallback; // if the entire name was illegal characters. sure.
  return s;
}

// does the actual copy work for one world. pulled out of extractWorld because that function
// was getting embarrassingly long
bool doExtract(const WorldEntry& world, const fs::path& logFile, std::ostream& log) {
  std::string safe      = safeFolderName(world.levelName, world.folderName);
  fs::path    exportDst = getExportsDir() / safe;

  log << "\n[phase] Extraction\n";
  log << "[info] World: \"" << world.levelName << "\"\n";
  log << "[info] Source: " << world.path.string() << "\n";
  log << "[info] Sanitised name: " << safe << "\n";
  log << "[info] Destination: " << exportDst.string() << "\n";

  clearScreen();
  printBanner();
  std::cout << "Extracting: " << col::cyan << world.levelName << col::reset << "\n";
  std::cout << col::grey << "From: " << world.path.string()  << col::reset << "\n";
  std::cout << col::grey << "To:   " << exportDst.string()   << col::reset << "\n\n";

  if (fs::exists(exportDst)) {
    std::cout << col::yellow << "Warning: destination already exists and will be overwritten.\n" << col::reset;
    log << "[warn] Destination exists. Overwriting.\n";
  }

  // count files before copy so we can verify after
  int      fileCount  = 0;
  uint64_t totalBytes = 0;
  for (const auto& item : fs::recursive_directory_iterator(world.path)) {
    if (item.is_regular_file()) {
      fileCount++;
      try { totalBytes += fs::file_size(item.path()); } catch (...) {}
    }
  }
  log << "[info] Files to copy: " << fileCount << " ("
      << std::fixed << std::setprecision(2) << (double)totalBytes / (1024.0 * 1024.0) << " MB)\n";

  std::cout << "Copying " << fileCount << " file(s)...\n";

  if (!copyDir(world.path, exportDst)) {
    std::cout << col::red << "Extraction failed.\n" << col::reset;
    std::cout << col::grey << logFile.string() << col::reset << "\n";
    log << "[error] copyDir failed.\n[result] FAILED\n";
    return false;
  }

  int copiedCount = 0;
  for (const auto& item : fs::recursive_directory_iterator(exportDst))
    if (item.is_regular_file()) copiedCount++;

  log << "[verify] Expected: " << fileCount << ", got: " << copiedCount << "\n";
  if (copiedCount != fileCount) {
    // mismatch. probably fine...
    log << "[warn] Count mismatch. Export may be incomplete.\n";
    std::cout << col::yellow << "Warning: file count mismatch (" << fileCount
              << " expected, " << copiedCount << " found). May be incomplete.\n" << col::reset;
  } else {
    log << "[verify] Counts match.\n";
  }

  log << "[result] SUCCESS\n";
  std::cout << col::green << "\"" << world.levelName << "\" extracted successfully!\n" << col::reset;
  std::cout << "Export: " << col::cyan << exportDst.string() << col::reset << "\n";
  std::cout << "Log:    " << col::grey << logFile.string()   << col::reset << "\n";
  return true;
}

// checks if the user pressed ctrl+l. we read a line and check if it's just that escape sequence.
// terminals send ctrl+l as the byte 0x0C (i think). reading it via getline is a bit awkward
// because it depends on the terminal doing something sane, which is not guaranteed.
// we treat the literal text "ctrl+l" and "l" as fallbacks in case the terminal ate the keystroke.
// is this perfect? no. does it work? probably.
bool isLegacyRequest(const std::string& s) {
  if (s.size() == 1 && s[0] == '\x0C') return true; // actual ctrl+l byte
  if (iequals(s, "ctrl+l")) return true;             // typed out literally, you know who you are
  if (iequals(s, "l"))      return true;             
  return false;
}

void extractWorld() {
  clearScreen();
  printBanner();

  std::string timestamp = nowString();
  fs::path    logsDir   = getLogsDir();

  // won't crash if this fails, we'll just log to a stringstream and dump at the end
  // actually we WILL crash if this fails because i didn't handle that. sorry future me
  fs::create_directories(logsDir);

  fs::path  logFile = logsDir / (timestamp + "_extract.log");
  std::ofstream log(logFile);

  if (!log.is_open()) {
    // logging is broken but we press on regardless
    std::cout << col::yellow << "Warning: couldn't open log file. Continuing without logging.\n" << col::reset;
  }

  log << "=== MCEX extraction log ===\n";
  log << "Timestamp: " << timestamp << "\n";
  log << "Working directory: " << fs::current_path().string() << "\n\n";

  // discovert]y

  std::cout << "Scanning for accounts...\n\n";
  log << "[phase] Account discovery\n";

  std::vector<UserAccount> accounts = discoverAccounts(log);

  // collect all worlds across all selected accounts
  std::vector<WorldEntry> worlds;
  bool usedLegacy = false;

  if (accounts.empty()) {
    // nothing found in the modern launcher path. offer legacy mode.
    clearScreen();
    printBanner();
    std::cout << col::yellow << "No accounts found in the Minecraft Bedrock launcher folder.\n" << col::reset;
    std::cout << "This could mean you're using the old Microsoft Store version.\n\n";
    std::cout << col::grey << "Press Ctrl+L for legacy mode (searches AppData\\Local UWP path)\n";
    std::cout << "or press Enter to quit." << col::reset << "\n\n";

    std::string resp = trim(getInput("> "));
    if (!isLegacyRequest(resp)) {
      log << "[result] No accounts found. User declined legacy mode.\n";
      return;
    }

    log << "[info] User selected legacy mode.\n";
    usedLegacy = true;

    fs::path legacyDir = getLegacyWorldsDir(log);
    if (legacyDir.empty() || !fs::exists(legacyDir)) {
      clearScreen();
      printBanner();
      std::cout << col::red << "Legacy path not found either.\n" << col::reset;
      std::cout << col::grey << legacyDir.string() << col::reset << "\n";
      std::cout << "Bedrock doesn't seem to be installed in any expected location.\n";
      log << "[result] Legacy path also missing. Giving up.\n";
      waitForEnter();
      return;
    }

    log << "[legacy] Using: " << legacyDir.string() << "\n";
    worlds = discoverWorlds(legacyDir, log);

  } else if (accounts.size() == 1) {
    // just scan this bih
    log << "[info] Single account found: " << accounts[0].accountId << "\n";
    worlds = discoverWorlds(accounts[0].worldsDir, log);

  } else {
    while (true) {
      clearScreen();
      printBanner();

      std::cout << "Multiple accounts found:\n";
      std::cout << "──────────────────────────────────\n";
      for (int i = 0; i < (int)accounts.size(); i++) {
        std::cout << "  [" << (i + 1) << "] Account: " << col::cyan << accounts[i].accountId << col::reset << "\n";
        std::cout << col::grey << "       " << accounts[i].worldsDir.string() << col::reset << "\n";
      }
      std::cout << "──────────────────────────────────\n\n";
      std::cout << "Enter account number(s) separated by spaces (e.g. 1 2), or 'all', or 'q' to quit:\n";

      std::string resp = trim(getInput("> "));

      if (iequals(resp, "q") || iequals(resp, "quit")) {
        log << "[result] User quit at account selection.\n";
        return;
      }

      std::vector<int> picked;

      if (iequals(resp, "all") || iequals(resp, "a")) {
        for (int i = 0; i < (int)accounts.size(); i++) picked.push_back(i);
      } else {
        // parse space-separated numbers. std::stoi throws on garbage, we catch it per token.
        std::istringstream ss(resp);
        std::string token;
        bool anyInvalid = false;
        while (ss >> token) {
          int n = 0;
          try { n = std::stoi(token); } catch (...) { anyInvalid = true; continue; }
          if (n < 1 || n > (int)accounts.size()) { anyInvalid = true; continue; }
          picked.push_back(n - 1); // convert to 0-indexed
        }
        if (anyInvalid) {
          std::cout << col::yellow << "Some selections were invalid and were ignored.\n" << col::reset;
        }
      }

      // deduplicate in case they typed "1 1 2" or something like idiots
      std::sort(picked.begin(), picked.end());
      picked.erase(std::unique(picked.begin(), picked.end()), picked.end());

      if (picked.empty()) {
        std::cout << col::red << "No valid accounts selected.\n" << col::reset;
        waitForEnter();
        continue;
      }

      for (int idx : picked) {
        log << "[info] Scanning account: " << accounts[idx].accountId << "\n";
        std::vector<WorldEntry> acctWorlds = discoverWorlds(accounts[idx].worldsDir, log);
        for (auto& w : acctWorlds) worlds.push_back(w);
      }
      break;
    }
  }

  // world selection shit

  if (worlds.empty()) {
    clearScreen();
    printBanner();
    std::cout << col::red << "No worlds found";
    if (usedLegacy) std::cout << " (legacy path)";
    std::cout << ".\n" << col::reset;
    std::cout << "Check the log for details: " << col::grey << logFile.string() << col::reset << "\n";
    log << "[result] No worlds found. Nothing to do.\n";
    waitForEnter();
    return;
  }

  while (true) {
    clearScreen();
    printBanner();

    if (usedLegacy)
      std::cout << col::yellow << "[Legacy mode]\n\n" << col::reset;

    std::cout << "Found " << worlds.size() << " world(s):\n";
    std::cout << "──────────────────────────────────\n";
    for (int i = 0; i < (int)worlds.size(); i++) {
      std::cout << "  [" << (i + 1) << "] " << col::cyan << worlds[i].levelName << col::reset;
      std::cout << col::grey << "  (" << worlds[i].folderName << ")" << col::reset << "\n";
    }
    std::cout << "──────────────────────────────────\n\n";
    std::cout << "Enter world number to extract, or 'q' to quit:\n";

    std::string choice = trim(getInput("> "));

    if (iequals(choice, "q") || iequals(choice, "quit")) {
      log << "[result] User quit without extracting.\n";
      return;
    }

    int picked = 0;
    try { picked = std::stoi(choice); } catch (...) { picked = 0; }

    if (picked < 1 || picked > (int)worlds.size()) {
      std::cout << col::red << "Invalid selection.\n" << col::reset;
      waitForEnter();
      continue;
    }

    bool ok = doExtract(worlds[picked - 1], logFile, log);
    waitForEnter();

    if (ok) return; 
    // if it failed, loop back so they can try another one
  }
}

int main() {
#ifdef PLATFORM_WINDOWS
  // without this the colour codes print as literal garbage on older windows terminals
  SetConsoleOutputCP(CP_UTF8);
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  GetConsoleMode(hOut, &mode);
  SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

  // mcex does exactly one thing so there's no real menu loop needed
  extractWorld();

  return 0; // unreachable if the user quits properly but the compiler would whine otherwise
}
