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

    // Check if the command is "exit"
    if (command.substr(0, 4) == "exit") {
      // Extract the exit status argument
      std::istringstream iss(command.substr(4));
      int exitStatus = 0;
      iss >> exitStatus;

      // If there's no argument, exit with status 0
      if (iss.fail()) {
        exitStatus = 0;
      }

      // Terminate the program with the specified exit status
      return exitStatus;
    }

    if (!std::cin.eof()) {
      std::cerr << command << ": command not found" << std::endl;
    } else {
      break;
    }
  }

  return 0;
}