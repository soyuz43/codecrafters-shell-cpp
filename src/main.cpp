#include <iostream>
#include <string>
#include <sstream>
#include <set>

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
      
      if (builtins.find(target) != builtins.end()) {
        std::cout << target << " is a shell builtin" << std::endl;
      } else {
        std::cout << target << ": not found" << std::endl;
      }
      continue;
    }

    std::cerr << cmd << ": command not found" << std::endl;
  }

  return 0;
}