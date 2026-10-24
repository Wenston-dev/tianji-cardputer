// Regenerate tianji/lunar_table.inc (lunar month lengths and leap months, 1900-2100) from ICU's
// Chinese calendar.  Usage: node tools/gen_lunar.js > tianji/lunar_table.inc
const fmt=new Intl.DateTimeFormat("en-u-ca-chinese",{timeZone:"UTC",year:"numeric",month:"numeric",day:"numeric"});
const parts=t=>{const p=Object.fromEntries(fmt.formatToParts(new Date(t)).map(x=>[x.type,x.value]));return {m:parseInt(p.month),leap:/bis/.test(p.month),d:parseInt(p.day),y:p.relatedYear};};
const DAY=86400000;let t=Date.UTC(1900,0,31);const out=[];
for(let Y=1900;Y<=2100;Y++){
  let v=0,leapM=0,leap30=0;let cur=null,len=0;const months=[];
  while(true){const p=parts(t);
    if(p.d==1){ if(cur)months.push({...cur,len}); if(p.m==1&&!p.leap&&months.length) break; cur=p;len=0;}
    len++;t+=DAY;}
  for(const mo of months){ if(mo.leap){leapM=mo.m;leap30=mo.len==30;} else if(mo.len==30) v|=0x8000>>(mo.m-1);}
  v|=leapM; if(leap30) v|=0x10000; out.push("0x"+v.toString(16).padStart(5,"0"));
}
console.log("static const uint32_t kLunarInfo[201] = {");
for(let i=0;i<out.length;i+=10) console.log("  "+out.slice(i,i+10).join(",")+",");
console.log("};");
