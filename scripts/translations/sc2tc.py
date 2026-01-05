import zhconv_rs
import json

JSON_KEY_TRANSL="translated strings"
JSON_KEY_CTXT="context"
JSON_KEY_OR="or"
JSON_KEY_TR="tr"
if __name__ == '__main__':
    json_sc = json.load(open("datasrc/languages/simplified_chinese.json"), strict=False)
    json_tc = json.load(open("datasrc/languages/traditional_chinese.json"), strict=False)
    def do_convert(text):
        return zhconv_rs.zhconv(text, "zh-HK")

    json_tc[JSON_KEY_TRANSL] = []
    for entry in json_sc[JSON_KEY_TRANSL]:
        add_entry = {JSON_KEY_OR: entry[JSON_KEY_OR], JSON_KEY_TR: do_convert(entry[JSON_KEY_TR])}
        if JSON_KEY_CTXT in entry:
            add_entry[JSON_KEY_CTXT] = entry[JSON_KEY_CTXT]
        json_tc[JSON_KEY_TRANSL].append(add_entry)
    json.dump(
		json_tc,
		open("datasrc/languages/traditional_chinese.json", 'w', encoding='utf-8'),
		ensure_ascii=False,
		indent="\t",
		separators=(',', ': '),
		sort_keys=True,
	)