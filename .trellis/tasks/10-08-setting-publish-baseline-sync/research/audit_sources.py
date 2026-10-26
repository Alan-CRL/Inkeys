from pathlib import Path
import re, json, hashlib
root=Path(__file__).resolve().parents[4]
research=root/'.trellis/tasks/10-08-setting-publish-baseline-sync/research'

def strip_comments_and_disabled(source):
    tokens=re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|/\*.*?\*/|//[^\n]*',re.S)
    source=tokens.sub(lambda m:m.group(1) if m.group(1) else ''.join('\n' if c=='\n' else ' ' for c in m.group(0)),source)
    lines=source.splitlines(True)
    stack=[]; disabled=False
    for i,line in enumerate(lines):
        directive=re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)',line)
        if directive:
            kind,tail=directive.groups()
            if kind in ('if','ifdef','ifndef'):
                iszero=kind=='if' and tail.strip()=='0'
                stack.append((disabled,iszero))
                disabled=disabled or iszero
            elif kind in ('else','elif') and stack:
                parent,iszero=stack[-1]
                if iszero:
                    disabled=parent
            elif kind=='endif' and stack:
                parent,iszero=stack.pop();disabled=parent
            lines[i]='\n' if line.endswith('\n') else ''
        elif disabled:
            lines[i]='\n' if line.endswith('\n') else ''
    return ''.join(lines)

key_re=re.compile(r'I18nKey\.([A-Za-z_][A-Za-z_0-9]*(?:\.[A-Za-z_][A-Za-z_0-9]*)*)')
ref_re=re.compile(r'(?:setlist|pptComSetlist|ddbInteractionSetList|Inkeys::config)\.\w+(?:\.\w+)*')
call_re=re.compile(r'(?<![\w])((?:\w+::)*[A-Za-z_]\w*)\s*\(')
keep_call=re.compile(r'^(?:ResetPositions|AutomaticPenResponseAvailable|Queue|WriteSetting|PptComWriteSetting|SyncDraw3State|SyncUi3BuiltInComponents|Set|GetUpdate|Clear|ChangeState|EraserPreferences|ProductHost|ProductRuntime|ShellExecute|Notify|Restart|Close|CreateShortcut|Disable|Ensure|OpenClipboard|EmptyClipboard|GlobalAlloc|GlobalLock|GlobalUnlock|GlobalFree|SetClipboard|CloseClipboard|Apply|Capture|Record|Start|EndShow|Show|Hide|Check|Load)')
page_re=re.compile(r'case\s+(settingTabEnum|settingPlugInTabEnum)::(\w+)\s*:')
report={'source_scope':{'publish':'e2c212d4d9cd68367c08363c450d392e9d63bcef','fluent':'834307a4'},'caveats':['Lexical extraction, not a full C++ preprocessor/parser. Excludes comments and #if 0 bodies. Other conditional variants are retained together. Counts do not prove UI/action equivalence. Source key families may be absent in fluent Setting.cpp because moved to Setting.Pages.cpp.'], 'sources':{}}
for branch in ('publish','fluent'):
    paths=list((root/'Inkeys/Cache/SettingBaseline'/branch).rglob('*.cpp'))
    sources=[]
    for path in sorted(paths):
        raw=path.read_bytes(); text=raw.decode('utf-8-sig'); active=strip_comments_and_disabled(text)
        case_hits=list(page_re.finditer(active));pages=[]
        for i,hit in enumerate(case_hits):
            end=case_hits[i+1].start() if i+1<len(case_hits) else len(active)
            segment=active[hit.end():end]
            first=active[:hit.start()].count('\n')+1
            if i+1==len(case_hits) and branch=='publish':
                tail=segment.find('                // 底栏：更新信息提示栏')
                if tail<0: tail=segment.find('// 底栏：更新信息提示栏')
                if tail>=0: segment=segment[:tail]
            def located(pattern):
                return [{'value':m.group(0) if pattern!=key_re else m.group(1), 'line':first+segment[:m.start()].count('\n')} for m in pattern.finditer(segment)]
            calls=sorted(set(m.group(1) for m in call_re.finditer(segment) if keep_call.match(m.group(1).split('::')[-1]) and not m.group(1).startswith('ImGui::')))
            pages.append({'page':hit.group(1)+'::'+hit.group(2),'line':first,'i18n_keys':sorted(set(m.group(1) for m in key_re.finditer(segment))),'configuration_refs':sorted(set(m.group(0) for m in ref_re.finditer(segment))), 'business_calls':calls,'key_occurrences':located(key_re)})
        sources.append({'path':str(path.relative_to(root)).replace('\\','/'),'sha256':hashlib.sha256(raw).hexdigest(),'line_count':text.count('\n')+1,'pages':pages,'all_keys':sorted(set(m.group(1) for m in key_re.finditer(active)))})
    report['sources'][branch]=sources
out=research/'content-source-summary.json'
out.write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').replace('\n','\r\n').encode('utf-8'))
print(json.dumps({'file':str(out),'pages':{b:sum(len(x['pages']) for x in ss) for b,ss in report['sources'].items()}},ensure_ascii=False))

def flat_json(value, prefix=''):
    if isinstance(value,dict):
        out={}
        for key,child in value.items(): out.update(flat_json(child,prefix+'.'+key if prefix else key))
        return out
    return {prefix:value}
i18n_delta={}
for lang in ('zh-CN','zh-TW','en-US'):
    by_branch={}
    for branch in ('publish','fluent'):
        path=root/'Inkeys/Cache/SettingBaseline'/branch/'Inkeys/src/i18n'/f'{lang}.jsonc'
        text=path.read_text(encoding='utf-8-sig')
        text=re.sub(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*',lambda m:m.group(1) or '',text)
        by_branch[branch]=flat_json(json.loads(text))
    p,f=by_branch['publish'],by_branch['fluent']
    relevant=lambda key:key.startswith(('SettingsUI.','Dialogs.'))
    i18n_delta[lang]={'added_to_publish':{k:p[k] for k in sorted(p.keys()-f.keys()) if relevant(k)},'removed_in_publish':{k:f[k] for k in sorted(f.keys()-p.keys()) if relevant(k)},'changed_in_publish':{k:{'fluent':f[k],'publish':p[k]} for k in sorted(p.keys()&f.keys()) if relevant(k) and p[k]!=f[k]}, 'note':'All Fluent SettingsUI.Design.* keys appear removed because that branch has no Fluent design layer. Retain still-used keys intentionally; all other deleted/changed visible strings follow published source.'}
(research/'content-i18n-delta.json').write_bytes((json.dumps(i18n_delta,ensure_ascii=False,indent=2)+'\n').replace('\n','\r\n').encode('utf-8'))
print(json.dumps({lang:{kind:len(vals) for kind,vals in result.items() if isinstance(vals,dict)} for lang,result in i18n_delta.items()},ensure_ascii=False))

# Source update footer has no page case and must be checked separately.
path=root/'Inkeys/Cache/SettingBaseline/publish/Inkeys/Inkeys/UI/Setting/Setting.cpp'
text=path.read_text(encoding='utf-8-sig')
start=text.find('// 底栏：更新信息提示栏')
end=text.find('ImGui::End();',start)
active=strip_comments_and_disabled(text[start:end])
report['published_global_update_status']={'line':text[:start].count('\n')+1, 'states':sorted(set(re.findall(r'AutomaticUpdateState\s*==\s*(?:AutomaticUpdateStateEnum::)?(Update\w+)',active))), 'i18n_keys':sorted(set(m.group(1) for m in key_re.finditer(active))), 'business_calls':sorted(set(m.group(1) for m in call_re.finditer(active) if keep_call.match(m.group(1).split('::')[-1]) and not m.group(1).startswith('ImGui::')))}
(research/'content-source-summary.json').write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').replace('\n','\r\n').encode('utf-8'))