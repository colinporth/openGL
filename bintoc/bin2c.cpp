// bin2c.cpp
//{{{  includes
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <stdio.h>
#include <assert.h>
#include <string>

using namespace std;
//}}}

int main(int argc, char** argv) {

	string filename = argv[1];

	// trim extension
	string root = filename;
	root.erase (root.find_last_of('.'), string::npos);

	// trim directory tree
	string symbol = root;
	symbol.erase (0, root.find_last_of('\\')+1);

	// open files
	FILE* inFile = fopen (filename.c_str(), "rb");
	FILE* outFile = fopen ((root + ".h").c_str(), "w");

	// header
	fprintf (outFile, "#pragma once\n");
	fprintf (outFile, "#include <stdint.h>\n");
	fprintf (outFile, "\n");
	fprintf (outFile, "const uint8_t %s[] {\n", symbol.c_str());

	// body
	unsigned long count = 0;
	while (!feof (inFile)) {
		uint8_t value;
		if (fread (&value, 1, 1, inFile) == 0)
			break;

		if (count % 16 == 0)
			fprintf (outFile, "  ");
		fprintf (outFile, "0x%.2X,", (int)value);

		count++;
		if (count % 16 == 0)
			fprintf (outFile, "\n");
		}

	// trailer
	fprintf (outFile, "};\n");

	// close files
	fclose (outFile);
	fclose (inFile);

	// info
	printf ("converted %s length %d to symbol %s\n", filename.c_str(), count, symbol.c_str());
	}
