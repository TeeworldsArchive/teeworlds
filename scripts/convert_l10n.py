import json
import os
import re
import time
import sys

from collections import defaultdict

os.chdir(os.path.dirname(os.path.realpath(sys.argv[0])) + "/..")

format = "{0:40} {1:8} {2:8} {3:8}".format
SOURCE_EXTS = [".c", ".cpp", ".h"]

JSON_KEY_AUTHORS="authors"
JSON_KEY_CLIENT="client strings"
JSON_KEY_SERVER="server strings"
JSON_KEY_CTXT="context"
JSON_KEY_FROM="from"
JSON_KEY_OR="or"
JSON_KEY_TR="tr"

SOURCE_LOCALIZE_RE=re.compile(br'Localize\("(?P<str>([^"\\]|\\.)*)"(, ?"(?P<ctxt>([^"\\]|\\.)*)")?\)')

def parse_source():
	l10n_client = defaultdict(lambda: str)
	l10n_server = defaultdict(lambda: str)

	def process_line(line, l10n):
		for match in SOURCE_LOCALIZE_RE.finditer(line):
			str_ = match.group('str').decode()
			ctxt = match.group('ctxt')
			if ctxt is not None:
				ctxt = ctxt.decode()
			l10n[(str_, ctxt)] = ""

	for root, dirs, files in os.walk("src"):
		for name in files:
			filename = os.path.join(root, name)
			
			if os.sep + "external" + os.sep in filename:
				continue

			if os.path.splitext(filename)[1] in SOURCE_EXTS:
				# HACK: Open source as binary file.
				# Necessary some of teeworlds source files
				# aren't utf-8 yet for some reason
				for line in open(filename, 'rb'):
					# process line
					if "client" in filename:
						process_line(line, l10n_client)
					else:
						process_line(line, l10n_server)
	return l10n_client, l10n_server

def load_languagefile(filename):
	return json.load(open(filename), strict=False) # accept \t tabs

def write_languagefile(outputfilename, l10n_client_src, l10n_server_src, old_l10n_data):
	translations_client = l10n_client_src.copy()
	translations_server = l10n_server_src.copy()

	translations_client.update({
		(t[JSON_KEY_OR], t.get(JSON_KEY_CTXT)): t[JSON_KEY_TR]
		for t in old_l10n_data[JSON_KEY_CLIENT]
		if t[JSON_KEY_TR] and translations_client.get((t[JSON_KEY_OR], t.get(JSON_KEY_CTXT))) != None
	})
	translations_server.update({
		(t[JSON_KEY_OR], t.get(JSON_KEY_CTXT)): t[JSON_KEY_TR]
		for t in old_l10n_data[JSON_KEY_SERVER]
		if t[JSON_KEY_TR] and translations_server.get((t[JSON_KEY_OR], t.get(JSON_KEY_CTXT))) != None
	})


	result = {JSON_KEY_CLIENT: [], JSON_KEY_SERVER: []}
	for entry in translations_client:
		if entry[0]:
			t_entry = {}
			t_entry[JSON_KEY_OR] = entry[0]
			t_entry[JSON_KEY_TR] = translations_client[entry]
			if entry[1] is not None:
				t_entry[JSON_KEY_CTXT] = entry[1]
			result[JSON_KEY_CLIENT].append(t_entry)

	for entry in translations_server:
		if entry[0]:
			t_entry = {}
			t_entry[JSON_KEY_OR] = entry[0]
			t_entry[JSON_KEY_TR] = translations_server[entry]
			if entry[1] is not None:
				t_entry[JSON_KEY_CTXT] = entry[1]
			result[JSON_KEY_SERVER].append(t_entry)
	result["authors"] = old_l10n_data["authors"]

	result[JSON_KEY_CLIENT].sort(key=lambda entry: entry[JSON_KEY_OR])
	result[JSON_KEY_SERVER].sort(key=lambda entry: entry[JSON_KEY_OR])

	json.dump(
		result,
		open(outputfilename, 'w', encoding='utf-8'),
		ensure_ascii=False,
		indent="\t",
		separators=(',', ': '),
		sort_keys=True,
	)

if __name__ == '__main__':
	l10n_client, l10n_server = parse_source()

	for filename in os.listdir("datasrc/languages"):
		try:
			if (os.path.splitext(filename)[1] == ".json"
					and filename != "index.json"):
				filename = "datasrc/languages/" + filename
				write_languagefile(filename, l10n_client, l10n_server, load_languagefile(filename))
		except Exception as e:
			print("Failed on {0}, re-raising for traceback".format(filename))
			raise
