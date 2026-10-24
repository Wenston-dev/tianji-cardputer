// Validate tianji/lunar_table.inc and the toLunar() algorithm against ICU's Chinese calendar
// for every day from 1901-02-19 to 2100-12-30.  Usage: node tools/verify_lunar.js
const fs=require("fs"),path=require("path");
const inc=fs.readFileSync(path.join(__dirname,"../tianji/lunar_table.inc"),"utf8");
const info=inc.match(/0x[0-9a-f]+/gi).map(Number);
const leapMonth=y=>info[y-1900]&0xf;
const leapDays=y=>leapMonth(y)?((info[y-1900]&0x10000)?30:29):0;
const monthDays=(y,m)=>(info[y-1900]&(0x10000>>m))?30:29;
function yearDays(y){let s=348;for(let i=0x8000;i>0x8;i>>=1)s+=(info[y-1900]&i)?1:0;return s+leapDays(y);}
function toLunar(Y,M,D){
  let off=Math.round((Date.UTC(Y,M-1,D)-Date.UTC(1900,0,31))/86400000);
  let y=1900,t;for(;y<2101&&off>0;y++){t=yearDays(y);off-=t;}
  if(off<0){off+=t;y--;}
  const lp=leapMonth(y);let isLeap=false,m=1;
  for(;m<13&&off>0;m++){
    if(lp>0&&m==lp+1&&!isLeap){--m;isLeap=true;t=leapDays(y);}else t=monthDays(y,m);
    if(isLeap&&m==lp+1)isLeap=false;
    off-=t;
  }
  if(off==0&&lp>0&&m==lp+1){ if(isLeap)isLeap=false; else {isLeap=true;--m;} }
  if(off<0){off+=t;--m;}
  return {y,m,d:off+1,leap:isLeap};
}
const fmt=new Intl.DateTimeFormat("en-u-ca-chinese",{timeZone:"UTC",year:"numeric",month:"numeric",day:"numeric"});
let bad=0,n=0;
for(let t=Date.UTC(1901,1,19);t<Date.UTC(2100,11,31);t+=86400000){
  const d=new Date(t);const p=Object.fromEntries(fmt.formatToParts(d).map(x=>[x.type,x.value]));
  const icuLeap=/bis/.test(p.month);const icuM=parseInt(p.month);const icuD=parseInt(p.day);
  const r=toLunar(d.getUTCFullYear(),d.getUTCMonth()+1,d.getUTCDate());n++;
  if(r.m!=icuM||r.d!=icuD||r.leap!=icuLeap){ if(0)console.log(d.toISOString().slice(0,10),JSON.stringify(r),p.month,p.day); bad++; }
}
console.log("checked",n,"days, mismatches:",bad);
process.exit(bad?1:0);
