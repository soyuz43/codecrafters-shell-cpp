#include <iostream>
#include <string>
#include <sstream>

int main() {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

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

    std::cerr << cmd << ": command not found" << std::endl;
  }

  return 0;
}