/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>
#include <engine/shared/datafile.h>
#include <engine/storage.h>

static void PrintUsage(const char *pProgram)
{
	dbg_msg("map_resave", "usage: %s [--v4|--v5] [--level=<1..22>] <in.map> <out.map>", pProgram);
}

int main(int argc, const char **argv)
{
	cmdline_fix(&argc, &argv);
	dbg_logger_stdout();

	int Version = 5;
	int Level = 3;
	const char *pInput = 0;
	const char *pOutput = 0;

	for(int i = 1; i < argc; i++)
	{
		if(str_comp(argv[i], "--v4") == 0)
			Version = 4;
		else if(str_comp(argv[i], "--v5") == 0)
			Version = 5;
		else if(str_comp_num(argv[i], "--level=", 8) == 0)
			Level = str_toint(argv[i] + 8);
		else if(argv[i][0] != '-' && !pInput)
			pInput = argv[i];
		else if(argv[i][0] != '-' && !pOutput)
			pOutput = argv[i];
	}

	if(!pInput || !pOutput)
	{
		PrintUsage(argv[0]);
		cmdline_free(argc, argv);
		return -1;
	}

	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_BASIC, argc, argv);
	if(!pStorage)
	{
		cmdline_free(argc, argv);
		return -1;
	}

	CDataFileReader Reader;
	if(!Reader.Open(pStorage, pInput, IStorage::TYPE_ALL))
	{
		cmdline_free(argc, argv);
		return -1;
	}

	CDataFileWriter Writer;
	if(!Writer.Open(pStorage, pOutput, Version))
	{
		dbg_msg("map_resave", "failed to open '%s' for writing", pOutput);
		Reader.Close();
		cmdline_free(argc, argv);
		return -1;
	}
	Writer.SetCompressLevel(Level);

	// add all items
	for(int Index = 0; Index < Reader.NumItems(); Index++)
	{
		int Type, ID;
		void *pPtr = Reader.GetItem(Index, &Type, &ID);
		int Size = Reader.GetItemSize(Index);
		Writer.AddItem(Type, ID, Size, pPtr);
	}

	// add all data
	for(int Index = 0; Index < Reader.NumData(); Index++)
	{
		void *pPtr = Reader.GetData(Index);
		int Size = Reader.GetDataSize(Index);
		Writer.AddData(Size, pPtr);
	}

	Reader.Close();
	Writer.Finish();

	dbg_msg("map_resave", "wrote '%s' as datafile v%d (level %d)", pOutput, Version, Level);

	cmdline_free(argc, argv);
	return 0;
}
