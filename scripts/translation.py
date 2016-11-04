#xgettext --keyword=_ --keyword="_P:1,2" --language=C -o ../infclass-translation/infclass.pot $(find ./src -name \*.cpp -or -name \*.h)

import sys, polib, json

reload(sys)
sys.setdefaultencoding('utf-8')

po = polib.pofile(sys.argv[1])

f = file(sys.argv[2], "w")

print >>f, '{"translation":['

for entry in po:
	print >>f, '\t{'
	print >>f, '\t\t"key": '+json.dumps(str(entry.msgid))+','
	if entry.msgstr:
		print >>f, '\t\t"value": '+json.dumps(str(entry.msgstr))+''
	else:
		for index in sorted(entry.msgstr_plural.keys()):
			print >>f, '\t\t"'+sys.argv[3+index]+'": '+json.dumps(entry.msgstr_plural[index])+','
	print >>f, '\t},'

print >>f, ']}'
