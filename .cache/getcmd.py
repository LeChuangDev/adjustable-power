import json, sys, os

db = json.load(open('build/compile_commands.json', encoding='utf-8'))
target = sys.argv[1].replace(os.sep, '/')
for e in db:
    f = e['file'].replace('\\', '/')
    if f.endswith(target):
        print('DIR=' + e['directory'])
        print('FILE=' + e['file'])
        print('CMD=' + e.get('command', ''))
        break
else:
    print('NOT FOUND: ' + target)
