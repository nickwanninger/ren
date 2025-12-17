#include <fstream>
#include <iostream>


int main(int argc, char **argv) {
  const char *input_file = argv[1];
  const char *output_file = argv[2];

  std::ifstream input(input_file, std::ios::binary);
  std::ofstream output(output_file, std::ios::binary);


  if (!input) {
    std::cerr << "Error: Unable to open input file.\n";
    return 1;
  }

  if (!output) {
    std::cerr << "Error: Unable to open output file.\n";
    return 1;
  }

  output << "unsigned char data[] = {\n";

  char byte;
  bool first = true;
  while (input.get(byte)) {
    if (!first) {
      output << ", ";
    }
    output << "0x" << std::hex << std::uppercase << (static_cast<unsigned>(byte) & 0xFF);
    first = false;
  }

  output << "\n};\n";



}