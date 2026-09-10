// Slide engine as an AudioWorklet processor, mirroring ../Engine.h.
// ENGINE_SRC is the processor source (loaded via a blob URL); WOB holds the medium recipes.
// Parameters arrive over the port as one object: tl, tr (ms), fb, bleed, inL, inR, tone, hpf,
// early, diff, mix, shape ('exp' loop or a chain), gains[], tap (false|'R'|'L'), hold, autoHold, wob{}.
export const ENGINE_SRC = String.raw`
const MAXMS=2100, STAGES=64;
class AP{ constructor(ms){ this.b=ms.map(m=>new Float32Array(Math.max(4,Math.round(m*0.001*sampleRate)))); this.i=ms.map(()=>0); this.g=0; }
  run(v){ const g=this.g; if(g<=0) return v; for(let s=0;s<this.b.length;s++){ const b=this.b[s]; let i=this.i[s]; const d=b[i]; const st=v+g*d; b[i]=st; i++; if(i===b.length) i=0; this.i[s]=i; v=d-g*st; } return v; }
  clear(){ for(const b of this.b) b.fill(0); } }
class Rng{ constructor(seed){ this.x=(seed*2147483647|0)||1; } next(){ this.x=(this.x*48271)%2147483647; return this.x/2147483647*2-1; } }
class E extends AudioWorkletProcessor{
  constructor(){ super(); const N=Math.ceil(MAXMS*0.001*sampleRate)+8; this.N=N;
    this.buf=[[],[]]; for(let c=0;c<2;c++) for(let k=0;k<STAGES;k++) this.buf[c].push(new Float32Array(N));
    this.w=0;
    const eT=[[2.3,5.1,9.7,15.3],[2.9,6.1,11.3,17.9]], lT=[[4.7,7.9,13.1,19.7],[5.3,8.9,14.9,22.3]];
    this.early=[new AP(eT[0]),new AP(eT[1])]; this.diff=[[],[]]; for(let c=0;c<2;c++) for(let k=0;k<STAGES;k++) this.diff[c].push(new AP(lT[c]));
    this.lp=[new Float32Array(STAGES),new Float32Array(STAGES)]; this.lp2=[new Float32Array(STAGES),new Float32Array(STAGES)]; this.hp=[new Float32Array(STAGES),new Float32Array(STAGES)];
    this.ph=[0,0.25]; this.rng=[new Rng(0.37),new Rng(0.71)]; this.rnd=[0,0]; this.rnd2=[0,0]; this.tide=[0,0];
    this.p={tl:350,tr:350,fb:0.46,bleed:0.15,inL:1,inR:1,tone:4200,hpf:20,early:0,diff:0.1,mix:0.5,shape:'exp',gains:[1],tap:false,hold:false,
            wob:{sine:0,sineHz:0,rand:0,randHz:0,lp:20000,lpTime:false,bits:0,hiss:0},clear:false,autoHold:false};
    this.env=0; this.peak=0.002; this.quiet=0; this.aHold=false;
    this.s={tl:350,tr:350,fb:0.46,tone:4200,mix:0.5,inL:1,inR:1}; // smoothed
    this.crush=0; this.hissRng=new Rng(0.9);
    this.port.onmessage=e=>{ Object.assign(this.p,e.data); if(!this.got){ this.got=true; const p=this.p; Object.assign(this.s,{tl:p.tl,tr:p.tr,fb:p.fb,tone:p.tone,mix:p.mix,inL:p.inL,inR:p.inR}); } if(this.p.clear){ this.clearAll(); this.p.clear=false; } }; }
  clearAll(){ for(const c of this.buf) for(const b of c) b.fill(0); for(const a of this.early) a.clear(); for(const c of this.diff) for(const a of c) a.clear(); for(const l of this.lp) l.fill(0); }
  read(buf, delaySamples){ const N=this.N; let r=this.w-delaySamples; while(r<0) r+=N; if(r>=N) r-=N; let i=r|0; if(i>=N) i=N-1; const f=r-i; const a=buf[i], b=buf[i+1>=N?0:i+1]; return a+(b-a)*f; }
  process(inputs,outputs){ const inp=inputs[0], out=outputs[0]; if(!out||!out[0]) return true;
    const xL=inp&&inp[0]?inp[0]:null, xR=inp&&inp[1]?inp[1]:xL; const n=out[0].length;
    const p=this.p, s=this.s, sr=sampleRate, kS=1-Math.exp(-1/(0.02*sr));
    const holdNow=p.hold||(p.autoHold&&this.aHold); const fb=holdNow?1:p.fb, toneT=holdNow?20000:p.tone;
    const W=p.wob; const bits=W.bits, q=bits?Math.pow(2,bits-1):0;
    const finite=p.shape!=='exp'&&!holdNow; const S=finite?Math.min(STAGES,p.gains.length):1;
    const th=Math.asin(Math.min(1,p.tap?0:p.bleed)), cb=Math.cos(th), sb=Math.sin(th);   // bleed is a rotation, so it never loses energy
    const eg=p.early*0.62, dg=p.diff*0.62; for(const a of this.early) a.g=eg; for(const c of this.diff) for(const a of c) a.g=dg;
    const drift=[0,0];
    for(let i=0;i<n;i++){
      s.tl+=(p.tl-s.tl)*kS; s.tr+=(p.tr-s.tr)*kS; s.fb+=(fb-s.fb)*kS; s.tone+=(toneT-s.tone)*kS; s.mix+=(p.mix-s.mix)*kS; s.inL+=(p.inL-s.inL)*kS; s.inR+=(p.inR-s.inR)*kS;
      // wobble per side: sine + filtered seeded noise, depth relative to time up to 250 ms
      for(let c=0;c<2;c++){ const T=c?s.tr:s.tl; const ref=Math.min(400,Math.max(40,T));
        this.ph[c]+=W.sineHz/sr; if(this.ph[c]>=1) this.ph[c]-=1;
        const kr=1-Math.exp(-2*Math.PI*Math.max(0.05,W.randHz)/sr); this.rnd[c]+=(this.rng[c].next()-this.rnd[c])*kr; this.rnd2[c]+=(this.rnd[c]-this.rnd2[c])*kr;
        let m=W.sine*Math.sin(2*Math.PI*this.ph[c])+W.rand*6*this.rnd2[c];
        if(W.tide){ const f=W.sineHz; const t=this.tide[c]+=1/sr; m=W.sine*(Math.sin(2*Math.PI*f*t+c*1.57)+0.6*Math.sin(2*Math.PI*f*1.0355*t+0.4)+0.35*Math.sin(2*Math.PI*f*0.518*t+1.9))/1.95; }
        drift[c]=m*ref; }
      const dL=Math.max(1,(s.tl+drift[0])*0.001*sr), dR=Math.max(1,(s.tr+drift[1])*0.001*sr);
      // read every stage
      let yL=0,yR=0; const rd=this.rd||(this.rd=[new Float32Array(STAGES),new Float32Array(STAGES)]);
      for(let k=0;k<S;k++){ const gk=finite?p.gains[k]:1; const a=this.read(this.buf[0][k],dL); rd[0][k]=a; yL+=gk*a;
        if(p.tap==='L'){ const b=this.read(this.buf[1][k],dL); rd[0][k]=0; yL=yL-gk*a+gk*b; const bb=this.read(this.buf[1][k],dR); rd[1][k]=bb; yR+=gk*bb; }
        else if(p.tap){ const b=this.read(this.buf[0][k],dR); rd[1][k]=0; yR+=gk*b; } else { const b=this.read(this.buf[1][k],dR); rd[1][k]=b; yR+=gk*b; } }
      // tone + loss filters
      const kT=s.tone>=19000?1:1-Math.exp(-2*Math.PI*s.tone/sr); const kH=p.hpf<=20?0:1-Math.exp(-2*Math.PI*p.hpf/sr);
      const lpL=W.lpTime?Math.min(W.lp,Math.max(1200,W.lp*Math.sqrt(60/s.tl))):W.lp, lpR=W.lpTime?Math.min(W.lp,Math.max(1200,W.lp*Math.sqrt(60/s.tr))):W.lp;
      const kL=lpL>=19000?1:1-Math.exp(-2*Math.PI*lpL/sr), kR=lpR>=19000?1:1-Math.exp(-2*Math.PI*lpR/sr);
      // process each stage's read on its way onward: diffuse, tone, loss, clip, crush
      const pr=this.pr||(this.pr=[new Float32Array(STAGES),new Float32Array(STAGES)]);
      for(let k=0;k<S;k++) for(let c=0;c<2;c++){ if((p.tap==='L'&&c===0)||(p.tap&&p.tap!=='L'&&c===1)){ pr[c][k]=0; continue; } let v=this.diff[c][k].run(rd[c][k]);
        this.lp[c][k]+=(v-this.lp[c][k])*kT; v=this.lp[c][k]; if(kH){ this.hp[c][k]+=(v-this.hp[c][k])*kH; v=v-this.hp[c][k]; } this.lp2[c][k]+=(v-this.lp2[c][k])*(c?kR:kL); v=this.lp2[c][k];
        { const a=Math.abs(v); if(a>0.5) v=Math.sign(v)*(0.5+Math.tanh((a-0.5)*2)*0.5); } if(q) v=Math.trunc(v*q)/q; pr[c][k]=v; }
      // inputs
      const hiss=W.hiss*this.hissRng.next();
      const xl=(xL?xL[i]:0), xr=(xR?xR[i]:0);
      { const lvl=Math.max(Math.abs(xl),Math.abs(xr)); this.env = lvl>this.env? lvl : this.env*(1-1/(0.05*sr));
        // adaptive threshold: 30 dB under the loudest thing in the last ~4 s, never below the noise floor
        this.peak = lvl>this.peak? lvl : Math.max(0.002, this.peak*(1-1/(4*sr)));
        const gate=Math.max(0.0015, this.peak*0.0316);
        if(this.env<gate){ this.quiet++; if(this.quiet>0.25*sr&&!this.aHold){ this.aHold=true; if(p.autoHold) this.port.postMessage({aHold:true}); } }
        else if(this.env>gate*2){ this.quiet=0; if(this.aHold){ this.aHold=false; if(p.autoHold) this.port.postMessage({aHold:false}); } } }
      const eL=this.early[0].run(xl*s.inL)+hiss, eR=this.early[1].run(xr*s.inR)+hiss;
            // write stage inputs
      for(let k=0;k<S;k++){ let inL,inR;
        if(k===0){ if(finite){ inL=eL; inR=eR; } else { inL=eL+s.fb*(cb*pr[0][0]+sb*pr[1][0]); inR=eR+s.fb*(cb*pr[1][0]-sb*pr[0][0]); } }
        else { inL=cb*pr[0][k-1]+sb*pr[1][k-1]; inR=cb*pr[1][k-1]-sb*pr[0][k-1]; }
        if(p.tap==='L') inL=0; else if(p.tap) inR=0;
        this.buf[0][k][this.w]=inL; this.buf[1][k][this.w]=inR; }
      this.w++; if(this.w>=this.N) this.w=0;
      const a=s.mix*Math.PI/2, gd=Math.cos(a), gw=Math.sin(a);
      out[0][i]=gd*xl+gw*yL; if(out[1]) out[1][i]=gd*xr+gw*yR;   // dry is the input as it arrived; the inL/inR routing only feeds the lines
    }
    return true; } }
registerProcessor('tide-engine',E);`;
export const WOB={
  sine:{sine:0.004, sineHz:0.7, rand:0,     randHz:0,  lp:20000,lpTime:false,bits:0, hiss:0},
  tape:{sine:0.0025,sineHz:0.7, rand:0.0012,randHz:6,  lp:9000, lpTime:false,bits:0, hiss:0.0003},
  oil: {sine:0.005, sineHz:2.3, rand:0.015, randHz:1.4,lp:2600, lpTime:false,bits:10,hiss:0.0004},
  bbd: {sine:0,     sineHz:0,   rand:0.0008,randHz:20, lp:8000, lpTime:true, bits:0, hiss:0.0005},
  tide:{sine:0.006, sineHz:0.3, rand:0,     randHz:0,  lp:20000,lpTime:false,bits:0, hiss:0, tide:true},
  clean:{sine:0,sineHz:0,rand:0,randHz:0,lp:20000,lpTime:false,bits:0,hiss:0},
};
