import json,io,re
p=r'C:\Users\Administrator\.codex\sessions\2026\09\14\rollout-2026-09-14T12-06-16-01a09e18-2085-78c1-90d6-fb188613e149.jsonl'
rows=[]
with io.open(p,encoding='utf-8',errors='replace') as f:
    for i,line in enumerate(f,1):
        try: r=json.loads(line)
        except Exception: continue
        pl=r.get('payload') or {}
        if r.get('type')!='response_item' or pl.get('type')!='function_call': continue
        if pl.get('name')!='exec_command': continue
        a=str(pl.get('arguments') or '')
        if '03-validation' not in a: continue
        try: cmd=json.loads(a).get('cmd','')
        except Exception: cmd=a
        mod=[]
        if 'ap.py' in cmd: mod.append('PATCH')
        if re.search(r"03-validation\.md['\"]\s*,\s*[\"']w", cmd): mod.append('WRITE')
        if 'write_text' in cmd: mod.append('WT')
        if re.search(r"io\.open\(p,\s*['\"]w", cmd): mod.append('W2')
        # detect append pattern
        if re.search(r"\+ *E\d+|rstrip\('\\n'\) *\+", cmd): mod.append('APPEND')
        rows.append((i,len(cmd),','.join(mod) or 'read/other',cmd))
for i,n,m,cmd in rows:
    print('%5d %6d %-14s %s'%(i,n,m,cmd.replace('\n',' ')[:95]))
print('total',len(rows))