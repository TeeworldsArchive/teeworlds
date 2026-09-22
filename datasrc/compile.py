import sys
from datatypes import *
import content
import network
try:
	import network7
except ImportError:
	network7 = None

def create_enum_table(names, num, start = "0"):
	lines = []
	lines += ["enum", "{"]
	lines += [f"\t{names[0]} = {start},"]
	for name in names[1:]:
		lines += ["\t%s,"%name]
	lines += ["\t%s" % num, "};"]
	return lines

def create_flags_table(names):
	lines = []
	lines += ["enum", "{"]
	i = 0
	for name in names:
		lines += ["\t%s = 1<<%d," % (name,i)]
		i += 1
	lines += ["};"]
	return lines

def EmitEnum(names, num):
	print("enum")
	print("{")
	print("\t%s=0," % names[0])
	for name in names[1:]:
		print("\t%s," % name)
	print("\t%s" % num)
	print("};")

def EmitFlags(names, num):
	print("enum")
	print("{")
	i = 0
	for name in names:
		print("\t%s = 1<<%d," % (name,i))
		i += 1
	print("};")

gen_network_header = False
gen_network_source = False
gen_network7_header = False
gen_network7_source = False
gen_client_content_header = False
gen_client_content_source = False
gen_server_content_header = False
gen_server_content_source = False

if "network_header" in sys.argv: gen_network_header = True
if "network_source" in sys.argv: gen_network_source = True
if "network7_header" in sys.argv: gen_network7_header = True
if "network7_source" in sys.argv: gen_network7_source = True
if "client_content_header" in sys.argv: gen_client_content_header = True
if "client_content_source" in sys.argv: gen_client_content_source = True
if "server_content_header" in sys.argv: gen_server_content_header = True
if "server_content_source" in sys.argv: gen_server_content_source = True

if gen_client_content_header:
	print("#ifndef CLIENT_CONTENT_HEADER")
	print("#define CLIENT_CONTENT_HEADER")

if gen_server_content_header:
	print("#ifndef SERVER_CONTENT_HEADER")
	print("#define SERVER_CONTENT_HEADER")


if gen_client_content_header or gen_server_content_header:
	# print some includes
	print('#include <engine/graphics.h>')
	print('#include <engine/sound.h>')

	# emit the type declarations
	contentlines = open("datasrc/content.py", "rb").readlines()
	order = []
	for line in contentlines:
		line = line.strip()
		if line[:6] == "class ".encode() and "(Struct)".encode() in line:
			order += [line.split()[1].split("(".encode())[0].decode("ascii")]
	for name in order:
		EmitTypeDeclaration(content.__dict__[name])

	# the container pointer
	print('extern CDataContainer *g_pData;')

	# enums
	EmitEnum(["IMAGE_%s"%i.name.value.upper() for i in content.container.images.items], "NUM_IMAGES")
	EmitEnum(["ANIM_%s"%i.name.value.upper() for i in content.container.animations.items], "NUM_ANIMS")
	EmitEnum(["SPRITE_%s"%i.name.value.upper() for i in content.container.sprites.items], "NUM_SPRITES")

if gen_client_content_source or gen_server_content_source:
	if gen_client_content_source:
		print('#include "client_data.h"')
	if gen_server_content_source:
		print('#include "server_data.h"')
	EmitDefinition(content.container, "datacontainer")
	print('CDataContainer *g_pData = &datacontainer;')


# NETWORK

def EmitNetworkHeader(net, guard, namespace = None):
	print("#ifndef %s" % guard)
	print("#define %s" % guard)

	if namespace:
		# The shared engine includes must stay outside the namespace; the raw
		# enums (teams, flags, skin parts, ...) go inside so that this frozen
		# protocol does not collide with the live one in the same translation unit.
		print('#include <engine/message.h>')
		print('#include <engine/shared/protocol_ex.h>')
		print("namespace %s {" % namespace)
		raw_header = net.RawHeader
		raw_header = raw_header.replace("#include <engine/message.h>", "")
		raw_header = raw_header.replace("#include <engine/shared/protocol_ex.h>", "")
		print(raw_header)
	else:
		print(net.RawHeader)

	for e in net.Enums:
		for l in create_enum_table(["%s_%s"%(e.name, v) for v in e.values], 'NUM_%sS'%e.name): print(l)
		print("")

	for e in net.Flags:
		for l in create_flags_table(["%s_%s" % (e.name, v) for v in e.values]): print(l)
		print("")

	non_extended = [o for o in net.Objects if o.ex is None]
	extended = [o for o in net.Objects if o.ex is not None]
	for l in create_enum_table(["NETOBJTYPE_EX"]+[o.enum_name for o in non_extended], "NUM_NETOBJTYPES"): print(l)
	for l in create_enum_table(["__NETOBJTYPE_UUID_HELPER"]+[o.enum_name for o in extended], "OFFSET_NETMSGTYPE_UUID", "OFFSET_GAME_UUID - 1"): print(l)
	print("")

	non_extended = [o for o in net.Messages if o.ex is None]
	extended = [o for o in net.Messages if o.ex is not None]
	for l in create_enum_table(["NETMSGTYPE_EX"]+[o.enum_name for o in non_extended], "NUM_NETMSGTYPES"): print(l)
	for l in create_enum_table(["__NETMSGTYPE_UUID_HELPER"]+[o.enum_name for o in extended], "OFFSET_MAPITEMTYPE_UUID", "OFFSET_NETMSGTYPE_UUID - 1"): print(l)
	print("")

	for item in net.Objects + net.Messages:
		for line in item.emit_declaration():
			print(line)
		print("")

	EmitEnum(["SOUND_%s"%i.name.value.upper() for i in content.container.sounds.items], "NUM_SOUNDS")
	EmitEnum(["WEAPON_%s"%i.name.value.upper() for i in content.container.weapons.id.items], "NUM_WEAPONS")

	print("""

class CNetObjHandler
{
	const char *m_pMsgFailedOn;
	char m_aMsgData[1024];
	const char *m_pObjFailedOn;
	int m_NumObjFailures;
	bool CheckInt(const char *pErrorMsg, int Value, int Min, int Max);
	bool CheckFlag(const char *pErrorMsg, int Value, int Mask);

	static const char *ms_apObjNames[];
	static int ms_aObjSizes[];
	static const char *ms_apMsgNames[];

public:
	CNetObjHandler();

	int ValidateObj(int Type, const void *pData, int Size);
	const char *GetObjName(int Type) const;
	int GetObjSize(int Type) const;
	const char *FailedObjOn() const;
	int NumObjFailures() const;
	
	const char *GetMsgName(int Type) const;
	void *SecureUnpackMsg(int Type, CUnpacker *pUnpacker);
	const char *FailedMsgOn() const;
};

""")

	if namespace:
		print("} // namespace %s" % namespace)

	print("#endif // %s" % guard)


def EmitNetworkSource(net, header_name, namespace = None):
	# create names
	lines = []

	lines += ['#include <engine/shared/protocol.h>']
	lines += ['#include <engine/message.h>']
	lines += ['#include "%s"' % header_name]

	if namespace:
		lines += ['namespace %s {' % namespace]

	lines += ['CNetObjHandler::CNetObjHandler()']
	lines += ['{']
	lines += ['\tm_pMsgFailedOn = "";']
	lines += ['\tm_pObjFailedOn = "";']
	lines += ['\tm_NumObjFailures = 0;']
	lines += ['}']
	lines += ['']
	lines += ['const char *CNetObjHandler::FailedObjOn() const { return m_pObjFailedOn; }']
	lines += ['int CNetObjHandler::NumObjFailures() const { return m_NumObjFailures; }']
	lines += ['const char *CNetObjHandler::FailedMsgOn() const { return m_pMsgFailedOn; }']
	lines += ['']
	lines += ['']
	lines += ['']
	lines += ['']

	lines += ['static const int max_int = 0x7fffffff;']
	lines += ['']

	lines += ['bool CNetObjHandler::CheckInt(const char *pErrorMsg, int Value, int Min, int Max)']
	lines += ['{']
	lines += ['\tif(Value < Min || Value > Max) { m_pObjFailedOn = pErrorMsg; m_NumObjFailures++; return false; }']
	lines += ['\treturn true;']
	lines += ['}']
	lines += ['']

	lines += ['bool CNetObjHandler::CheckFlag(const char *pErrorMsg, int Value, int Mask)']
	lines += ['{']
	lines += ['\tif((Value&Mask) != Value) { m_pObjFailedOn = pErrorMsg; m_NumObjFailures++; return false; }']
	lines += ['\treturn true;']
	lines += ['}']
	lines += ['']

	lines += ["const char *CNetObjHandler::ms_apObjNames[] = {"]
	lines += ['\t"invalid",']
	lines += ['\t"%s",' % o.name for o in net.Objects]
	lines += ['\t""', "};", ""]

	lines += ["int CNetObjHandler::ms_aObjSizes[] = {"]
	lines += ['\t0,']
	lines += ['\tsizeof(%s),' % o.struct_name for o in net.Objects]
	lines += ['\t0', "};", ""]


	lines += ['const char *CNetObjHandler::ms_apMsgNames[] = {']
	lines += ['\t"invalid",']
	for msg in net.Messages:
		lines += ['\t"%s",' % msg.name]
	lines += ['\t""']
	lines += ['};']
	lines += ['']

	lines += ['const char *CNetObjHandler::GetObjName(int Type) const']
	lines += ['{']
	lines += ['\tif(Type < 0 || Type >= NUM_NETOBJTYPES) return "(out of range)";']
	lines += ['\treturn ms_apObjNames[Type];']
	lines += ['};']
	lines += ['']

	lines += ['int CNetObjHandler::GetObjSize(int Type) const']
	lines += ['{']
	lines += ['\tif(Type < 0 || Type >= NUM_NETOBJTYPES) return 0;']
	lines += ['\treturn ms_aObjSizes[Type];']
	lines += ['};']
	lines += ['']


	lines += ['const char *CNetObjHandler::GetMsgName(int Type) const']
	lines += ['{']
	lines += ['\tif(Type < 0 || Type >= NUM_NETMSGTYPES) return "(out of range)";']
	lines += ['\treturn ms_apMsgNames[Type];']
	lines += ['};']
	lines += ['']


	# create validate tables
	lines += ['int CNetObjHandler::ValidateObj(int Type, const void *pData, int Size)']
	lines += ['{']
	lines += ['\tswitch(Type)']
	lines += ['\t{']
	lines += ['\tcase NETOBJTYPE_EX:']
	lines += ['\t{']
	lines += ['\t\treturn 0;']
	lines += ['\t}']

	for item in net.Objects:
		base_item = None
		if item.base:
			base_item = next(i for i in net.Objects if i.name == item.base)
		for line in item.emit_validate(base_item):
			lines += ["\t" + line]
		lines += ['\t']
	lines += ['\t}']
	lines += ['\treturn -1;']
	lines += ['};']
	lines += ['']

	lines += ['void *CNetObjHandler::SecureUnpackMsg(int Type, CUnpacker *pUnpacker)']
	lines += ['{']
	lines += ['\tm_pMsgFailedOn = 0;']
	lines += ['\tm_pObjFailedOn = 0;']
	lines += ['\tswitch(Type)']
	lines += ['\t{']


	for item in net.Messages:
		for line in item.emit_unpack():
			lines += ["\t" + line]
		lines += ['\t']

	lines += ['\tdefault:']
	lines += ['\t\tm_pMsgFailedOn = "(type out of range)";']
	lines += ['\t\tbreak;']
	lines += ['\t}']
	lines += ['\t']
	lines += ['\tif(pUnpacker->Error())']
	lines += ['\t\tm_pMsgFailedOn = "(unpack error)";']
	lines += ['\t']
	lines += ['\tif(m_pMsgFailedOn || m_pObjFailedOn) {']
	lines += ['\t\tif(!m_pMsgFailedOn)']
	lines += ['\t\t\tm_pMsgFailedOn = "";']
	lines += ['\t\tif(!m_pObjFailedOn)']
	lines += ['\t\t\tm_pObjFailedOn = "";']
	lines += ['\t\treturn 0;']
	lines += ['\t}']
	lines += ['\tm_pMsgFailedOn = "";']
	lines += ['\tm_pObjFailedOn = "";']
	lines += ['\treturn m_aMsgData;']
	lines += ['};']
	lines += ['']

	lines += ['void RegisterGameUuids(CUuidManager *pManager)']
	lines += ['{']

	for item in net.Objects + net.Messages:
		if item.ex is not None:
			lines += ['\tpManager->RegisterName(%s, "%s");' % (item.enum_name, item.ex)]
	lines += ['}']

	if namespace:
		lines += ['} // namespace %s' % namespace]

	for l in lines:
		print(l)


if gen_network_header:
	EmitNetworkHeader(network, "GAME_GENERATED_PROTOCOL_H")

if gen_network7_header:
	if network7 is None:
		raise RuntimeError("network7 module not available")
	EmitNetworkHeader(network7, "GAME_GENERATED_PROTOCOL7_H", "protocol7")

if gen_network_source:
	EmitNetworkSource(network, "protocol.h")

if gen_network7_source:
	if network7 is None:
		raise RuntimeError("network7 module not available")
	EmitNetworkSource(network7, "protocol7.h", "protocol7")

if gen_client_content_header or gen_server_content_header:
	print("#endif")