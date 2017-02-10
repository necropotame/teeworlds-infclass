#xgettext --keyword=_ --keyword="_P:1,2" --language=C -o ../infclass-translation/infclass.pot $(find ./src -name \*.cpp -or -name \*.h)

import sys, polib, json

def ConvertPo2Json(languageCode, plurals):
	poFileName = "../infclass-translation/infclasspot_"+languageCode+".po"
	jsonFileName = "./data/languages/"+languageCode+".json"

	po = polib.pofile(poFileName)

	f = open(jsonFileName, "w")

	print('{"translation":[', end="\n", file=f)

	for entry in po:
		if entry.msgstr:
			print('\t{', end="\n", file=f)
			print('\t\t"key": '+json.dumps(str(entry.msgid))+',', end="\n", file=f)
			print('\t\t"value": '+json.dumps(str(entry.msgstr))+'', end="\n", file=f)
			print('\t},', end="\n", file=f)
		elif entry.msgstr_plural.keys():
			print('\t{', end="\n", file=f)
			print('\t\t"key": '+json.dumps(str(entry.msgid_plural))+',', end="\n", file=f)
			for index in sorted(entry.msgstr_plural.keys()):
				print('\t\t"'+plurals[index]+'": '+json.dumps(entry.msgstr_plural[index])+',', end="\n", file=f)
			print('\t},', end="\n", file=f)

	print(']}', end="\n", file=f)

ConvertPo2Json("ar", ["zero", "one", "two", "few", "many", "other"])
ConvertPo2Json("cs", ["one", "few", "other"])
ConvertPo2Json("de", ["one", "other"])
ConvertPo2Json("en", ["one", "other"])
ConvertPo2Json("el", ["one", "other"])
ConvertPo2Json("es", ["one", "other"])
ConvertPo2Json("fr", ["one", "other"])
ConvertPo2Json("hr", ["one", "few", "other"])
ConvertPo2Json("hu", ["one", "other"])
ConvertPo2Json("it", ["one", "other"])
ConvertPo2Json("ja", ["other"])
ConvertPo2Json("la", ["one", "other"])
ConvertPo2Json("nl", ["one", "other"])
ConvertPo2Json("pl", ["one", "few", "many", "other"])
ConvertPo2Json("pt", ["one", "other"])
ConvertPo2Json("ru", ["one", "few", "many", "other"])
ConvertPo2Json("uk", ["one", "few", "other"])
ConvertPo2Json("fa", ["one", "other"])
