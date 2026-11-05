// design-dump.js DESIGN_SCRIPT OUT.json: evaluates a design component script (claude.ai design,
// the settings card) with a stub DCLogic for the three voices in the E-ink scheme and writes
// its themes (t), interleaf tokens (il) and per-voice settings styles (k). Input to gen-theme.py.
const fs=require('fs');
global.DCLogic=class{constructor(p){this.props=p||{};this.state={};} setState(){}};
function load(f){const src=fs.readFileSync(f,'utf8');return new Function('DCLogic',src+'\n;return Component;')(DCLogic);}
const C=load(process.argv[2]);
const out={};
for(const v of ['white','editorial','stationery']){
  const c=new C({scheme:'eink',pen:'1',theme:v});
  const TH=c.themes(); const t=TH[v];
  const il={}; t.ilv.split(';').forEach(kv=>{const i=kv.indexOf(':'); if(i>0) il[kv.slice(0,i)]=kv.slice(i+1);});
  let vals=null; try{ c.state={...c.state}; vals=c.renderVals(); }catch(e){ vals={err:String(e).slice(0,200)}; }
  out[v]={t, il, k: vals && vals.k, keys: vals?Object.keys(vals):null};
}
fs.writeFileSync(process.argv[3],JSON.stringify(out,null,1));
console.log(Object.keys(out.white.t).length, out.white.k?Object.keys(out.white.k).length:out.white.keys);
