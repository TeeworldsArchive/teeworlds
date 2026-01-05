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
JSON_KEY_TRANSL="translated strings"
JSON_KEY_UNTRANSL="needs translation"
JSON_KEY_OLDTRANSL="old translations"
JSON_KEY_CTXT="context"
JSON_KEY_OR="or"
JSON_KEY_TR="tr"

SOURCE_LOCALIZE_RE=re.compile(br'Localize\("(?P<str>([^"\\]|\\.)*)"(, ?"(?P<ctxt>([^"\\]|\\.)*)")?\)')

def parse_source():
	l10n = defaultdict(lambda: str)

	def process_line(line, filename, lineno):
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
				for lineno, line in enumerate(open(filename, 'rb')):
					# process line
					process_line(line, filename, lineno)
	return l10n

def load_languagefile(filename):
	return json.load(open(filename), strict=False) # accept \t tabs

def write_languagefile(outputfilename, l10n_src, old_l10n_data):
	translations = l10n_src.copy()
	for type_ in (
		JSON_KEY_OLDTRANSL,
		JSON_KEY_UNTRANSL,
		JSON_KEY_TRANSL,
	):
		if type_ not in old_l10n_data:
			continue
		translations.update({
			(t[JSON_KEY_OR], t.get(JSON_KEY_CTXT)): t[JSON_KEY_TR]
			for t in old_l10n_data[type_]
			if t[JSON_KEY_TR] and translations.get((t[JSON_KEY_OR], t.get(JSON_KEY_CTXT))) != None
		})

	result = {JSON_KEY_TRANSL: []}
	for entry in translations:
		if entry[0]:
			t_entry = {}
			t_entry[JSON_KEY_OR] = entry[0]
			t_entry[JSON_KEY_TR] = translations[entry]
			if entry[1] is not None:
				t_entry[JSON_KEY_CTXT] = entry[1]
			result[JSON_KEY_TRANSL].append(t_entry)
	result["authors"] = old_l10n_data["authors"]

	json.dump(
		result,
		open(outputfilename, 'w', encoding='utf-8'),
		ensure_ascii=False,
		indent="\t",
		separators=(',', ': '),
		sort_keys=True,
	)

if __name__ == '__main__':
	l10n_src = parse_source()

	for filename in os.listdir("datasrc/languages"):
		try:
			if (os.path.splitext(filename)[1] == ".json"
					and filename != "index.json"):
				filename = "datasrc/languages/" + filename
				write_languagefile(filename, l10n_src, load_languagefile(filename))
		except Exception as e:
			print("Failed on {0}, re-raising for traceback".format(filename))
			raise
