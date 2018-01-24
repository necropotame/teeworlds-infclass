#xgettext --keyword=_ --keyword="_P:1,2" --language=C --from-code=UTF-8 -o ../infclass-translation/infclasspot.po $(find ./src -name \*.cpp -or -name \*.h)

import sys, polib, json, os

reload(sys)
sys.setdefaultencoding('utf-8')

def ConvertPo2Json(languageCode, plurals):
	if os.path.isfile("../infclass-translation/infclasspot_"+languageCode+".po"):
		poFileName = "../infclass-translation/infclasspot_"+languageCode+".po"
		jsonFileName = "./data/languages/"+languageCode+".json"

		po = polib.pofile(poFileName)

		f = file(jsonFileName, "w")

		print >>f, '{"translation":['

		for entry in po:
			if entry.msgstr:
				print >>f, '\t{'
				print >>f, '\t\t"key": '+json.dumps(str(entry.msgid))+','
				print >>f, '\t\t"value": '+json.dumps(str(entry.msgstr))+''
				print >>f, '\t},'
			elif entry.msgstr_plural.keys():
				print >>f, '\t{'
				print >>f, '\t\t"key": '+json.dumps(str(entry.msgid_plural))+','
				for index in sorted(entry.msgstr_plural.keys()):
					print >>f, '\t\t"'+plurals[index]+'": '+json.dumps(entry.msgstr_plural[index])+','
				print >>f, '\t},'

		print >>f, ']}'

ConvertPo2Json("ar", ["zero", "one", "two", "few", "many", "other"])
ConvertPo2Json("bg", ["one", "other"])
ConvertPo2Json("cs", ["one", "few", "other"])
ConvertPo2Json("de", ["one", "other"])
ConvertPo2Json("el", ["one", "other"])
ConvertPo2Json("es", ["one", "other"])
ConvertPo2Json("fr", ["one", "other"])
ConvertPo2Json("hr", ["one", "few", "other"])
ConvertPo2Json("sr-Latn", ["one", "few", "other"])
ConvertPo2Json("hu", ["one", "other"])
ConvertPo2Json("it", ["one", "other"])
ConvertPo2Json("ja", ["other"])
ConvertPo2Json("la", ["one", "other"])
ConvertPo2Json("nl", ["one", "other"])
ConvertPo2Json("pl", ["one", "few", "many", "other"])
ConvertPo2Json("pt", ["one", "other"])
ConvertPo2Json("ru", ["one", "few", "many", "other"])
ConvertPo2Json("sah", ["other"])
ConvertPo2Json("uk", ["one", "few", "other"])
ConvertPo2Json("fa", ["one", "other"])
ConvertPo2Json("tl", ["one", "other"])
ConvertPo2Json("tr", ["one", "other"])
ConvertPo2Json("zh-Hans", ["other"])
