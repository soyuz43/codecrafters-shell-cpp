#include <iostream>
#include <string>
#include <sstream>
#include <set>
#include <vector>
#include <cstdlib>

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define X_OK 1
#define access _access
const char PATH_SEPARATOR = ';';
#else
#include <unistd.h>
const char PATH_SEPARATOR = ':';
#endif

#ifdef _WIN32
#include <sys/stat.h>
bool is_executable(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  // On Windows, check if file exists and is not a directory
  return (st.st_mode & _S_IFREG) != 0;
}
#else
bool is_executable(const std::string& path) {
  return access(path.c_str(), X_OK) == 0;
}
#endif

std::vector<std::string> split_path(const std::string& path_env) {
  std::vector<std::string> directories;
  std::stringstream ss(path_env);
  std::string dir;
  
  while (std::getline(ss, dir, PATH_SEPARATOR)) {
    if (!dir.empty()) {
      directories.push_back(dir);
    }
  }
  
  return directories;
}

std::string find_in_path(const std::string& command) {
  const char* path_env = std::getenv("PATH");
  if (!path_env) {
    return "";
  }
  
  std::vector<std::string> directories = split_path(path_env);
  
  for (const auto& dir : directories) {
    std::string full_path = dir + "/" + command;
    
    // Check if file exists and is executable
    if (access(full_path.c_str(), F_OK) == 0 && is_executable(full_path)) {
      return full_path;
    }
  }
  
  return "";
}

int main() {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // Define builtin commands
  std::set<std::string> builtins = {"echo", "exit", "type"};

  while (true) {
    std::cout << "$ ";
    std::string command;
    std::getline(std::cin, command);

    // Check for EOF
    if (std::cin.eof()) {
      break;
    }

    // Tokenize the command
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;

    // Check if the command is "exit"
    if (cmd == "exit") {
      int exitStatus = 0;
      iss >> exitStatus;
      return exitStatus;
    }

    // Check if the command is "echo"
    if (cmd == "echo") {
      std::string arg;
      bool first = true;
      while (iss >> arg) {
        if (!first) std::cout << " ";
        std::cout << arg;
        first = false;
      }
      std::cout << std::endl;
      continue;
    }

    // Check if the command is "type"
    if (cmd == "type") {
      std::string target;
      iss >> target;
      
      // First check if it's a builtin
      if (builtins.find(target) != builtins.end()) {
        std::cout << target << " is a shell builtin" << std::endl;
      } else {
        // Search in PATH
        std::string path = find_in_path(target);
        if (!path.empty()) {
          std::cout << target << " is " << path << std::endl;
        } else {
          std::cout << target << ": not found" << std::endl;
        }
      }
      continue;
    }

    std::cerr << cmd << ": command not found" << std::endl;
  }

  return 0;
}