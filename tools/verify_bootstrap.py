"""Read-only verification of the recorded local dual-host acceptance evidence."""
import argparse
import hashlib
import json
from pathlib import Path

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--report',type=Path,default=Path('reports/bootstrap'));args=ap.parse_args();root=Path(__file__).resolve().parents[1];report=args.report.resolve();checks=[]
    def check(name,ok):
        checks.append(dict(name=name,passed=bool(ok)))
        if not ok:raise AssertionError(name)
    def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
    worker=root/'build/scdat_radiation_worker.exe';worker_hash=sha(worker)
    check('all thirty-three independent CTests passed','100% tests passed, 0 tests failed out of 33' in (report/'ctest-final.log').read_text(errors='replace'))
    service=(report/'service-runtime-final.log').read_text(errors='replace');check('real service failure paths and seeded repeatability passed','Checks complete: 0 failure(s)' in service and 'FAIL ' not in service and service.count('PASS ')>=15)
    results=[]
    for case in ('standalone-release','embedded-release','tkmaster-default-release','bundle-style-release'):
        summary=read(report/case/'summary.json');check(case+' all native checks passed',summary['passed'] and summary['checks'] and all(x['passed'] for x in summary['checks']))
        job=report/case/'run';r=read(job/'result.json');check(case+' actual launch/Worker/runtime identities',r['schema']=='scdat.radiation-result/2' and r['status']=='completed' and r['histories']==10000 and r['project_sha256']==sha(job/'project.json') and r['worker']['sha256']==worker_hash and r['engine_version']=='11.4.3' and len(r['targets'])==4)
        check(case+' original event moments retained',all(r['score_moments'][t['id']]['ionizing']['count']==10000 and t['mass_kg']>0 and t['tid_Gy']>=0 and t['tid_standard_error_Gy']>=0 for t in r['targets']))
        check(case+' no Worker navigation warnings',not any(word in (job/'worker.log').read_text(errors='replace') for word in ('G4Exception','WARNING','GeomNav1002','Stuck Track')))
        results.append(r)
    for label,r in zip(('embedded','actual TKMaster','relocated bundle'),results[1:]):check(label+' identical seeded dose and moments to standalone',r['targets']==results[0]['targets'] and r['score_moments']==results[0]['score_moments'])
    check('host scene preserved byte for byte',(report/'tkmaster-default-release/scene-before.json').read_bytes()==(report/'tkmaster-default-release/scene-after.json').read_bytes())
    check('installed SDK consumer executes','service=ready' in (report/'consumer-run.log').read_text())
    bundle=root/'dist/SpaceRad-0.1.0-local';check('staged Worker matches independently built Worker',sha(bundle/'scdat_radiation_worker.exe')==worker_hash)
    check('bundle platform plugin and relative runtime worker exist',(bundle/'platforms/qwindows.dll').is_file() and read(bundle/'runtime.json')['worker']=='scdat_radiation_worker.exe')
    paths={p for p in report.rglob('*') if p.is_file() and p.name not in ('acceptance.json','independent-rehash.json')}
    for directory in ('src','include','engine','app','cmake','tests','tools','integration','docs'):
        paths.update(p for p in (root/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    paths.update(p for p in root.iterdir() if p.is_file() and p.name not in ('runtime.local.json','CMakeUserPresets.json'))
    paths.update((worker,root/'build/spacerad.exe',root/'build/spacerad_embedded_example.exe'))
    paths.update(p for p in bundle.rglob('*') if p.is_file())
    records=[dict(path=p.relative_to(root).as_posix(),size_bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(paths)]
    output=report/'acceptance.json'
    if output.exists():raise RuntimeError('Refusing existing acceptance capture')
    output.write_text(json.dumps(dict(schema='spacerad.bootstrap-acceptance/1',passed=True,checks=checks,files=records,scope='Local Windows source/build/install, actual dual-host and TKMaster integration, runtime relocation and numerical repeatability. External datasets are configured separately; no clean-machine, license, public-release or physics certification.'),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'Bootstrap acceptance: {len(checks)} checks, {len(records)} captured files')

if __name__=='__main__':main()
