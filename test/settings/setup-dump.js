// setup-dump.js DESIGN_SCRIPT OUT.json: evaluates the first-run setup's design script (claude.ai
// design "E-ink Shell", EinkSetup) with a stub DCLogic for the three voices in the E-ink
// scheme (pen 1) and writes its interleaf tokens (il), per-voice styles (k, functions dropped) and
// its sample data (packs). Input to gen-setup-theme.py.
const fs=require('fs');
global.DCLogic=class{constructor(p){this.props=p||{};this.state={};} setState(){}};
function load(f){const src=fs.readFileSync(f,'utf8');return new Function('DCLogic',src+'\n;return Component;')(DCLogic);}
const C=load(process.argv[2]);
const out={};
for(const v of ['white','editorial','stationery']){
  const c=new C({scheme:'eink',pen:'1',theme:v});
  const il={}; c.ilVars(v,'eink','1').split(';').forEach(kv=>{const i=kv.indexOf(':'); if(i>0) il[kv.slice(0,i)]=kv.slice(i+1);});
  const k=JSON.parse(JSON.stringify(c.voiceStyles()[v]));       // drops eyebrow
  out[v]={il,k};
}
out.packs=new C({}).packList();
fs.writeFileSync(process.argv[3],JSON.stringify(out,null,1));
console.log(Object.keys(out.white.k).length,'keys,',out.packs.length,'packs');
