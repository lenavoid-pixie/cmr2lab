/* CMR2 lab viewer - real SDL2, real camera, theater controls.
   First actual port code. Headless-capable (SDL_VIDEODRIVER=dummy). */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { float x,y,z; int used; } Pt;

static int NF=12;            /* floats per record */
static Pt *PTS=NULL; static int NP=0;

/* ---- camera: this is the thing the original never gave you ---- */
typedef struct {
    float px,py,pz;          /* eye */
    float tx,ty,tz;          /* target */
    float yaw,pitch,dist;    /* orbit */
    float fov;
    int   mode;              /* 0 = orbit, 1 = free */
} Cam;

static void orbit_eye(Cam*c,float*ex,float*ey,float*ez){
    float cp=cosf(c->pitch), sp=sinf(c->pitch);
    *ex=c->tx+c->dist*cp*sinf(c->yaw);
    *ey=c->ty+c->dist*sp;
    *ez=c->tz+c->dist*cp*cosf(c->yaw);
}

/* ---- theater timeline: pause / step / speed ---- */
typedef struct { int frame, last; int playing; float speed; } Time;
static void t_step(Time*t){ if(t->frame<t->last)t->frame++; }
static void t_back(Time*t){ if(t->frame>0)t->frame--; }

static int project(Cam*c,int w,int h,float x,float y,float z,int*ox,int*oy){
    float ex,ey,ez;
    if(c->mode==0) orbit_eye(c,&ex,&ey,&ez); else { ex=c->px;ey=c->py;ez=c->pz; }
    float fx=c->tx-ex, fy=c->ty-ey, fz=c->tz-ez;
    float fl=sqrtf(fx*fx+fy*fy+fz*fz); if(fl<1e-6f) return 0;
    fx/=fl; fy/=fl; fz/=fl;
    float ux=0,uy=1,uz=0;
    float rx=fy*uz-fz*uy, ry=fz*ux-fx*uz, rz=fx*uy-fy*ux;
    float rl=sqrtf(rx*rx+ry*ry+rz*rz); if(rl<1e-6f) return 0;
    rx/=rl; ry/=rl; rz/=rl;
    float vx=ry*fz-rz*fy, vy=rz*fx-rx*fz, vz=rx*fy-ry*fx;
    float dx=x-ex, dy=y-ey, dz=z-ez;
    float cz=dx*fx+dy*fy+dz*fz; if(cz<0.02f) return 0;
    float cx=dx*rx+dy*ry+dz*rz, cy=dx*vx+dy*vy+dz*vz;
    float f=1.0f/tanf(c->fov*0.5f);
    *ox=(int)(w*0.5f + (cx/cz)*f*w*0.5f);
    *oy=(int)(h*0.5f - (cy/cz)*f*h*0.5f);
    return 1;
}

int main(int argc,char**argv){
    const char*bin = argc>1?argv[1]:"/root/.lena_cmr2/work/cloud.bin";
    const char*out = argc>2?argv[2]:"/root/frame.ppm";
    int W = argc>3?atoi(argv[3]):900;
    int H = argc>4?atoi(argv[4]):600;
    int ax = argc>5?atoi(argv[5]):0;
    int ay = argc>6?atoi(argv[6]):1;
    int az = argc>7?atoi(argv[7]):2;

    FILE*f=fopen(bin,"rb");
    if(!f){fprintf(stderr,"no %s\n",bin);return 1;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    int nf=(int)(sz/4); float*F=malloc(sz); fread(F,4,nf,f); fclose(f);
    fprintf(stderr,"loaded %d floats (%d records of %d)\n",nf,nf/NF,NF);

    int recs=nf/NF;
    PTS=malloc(sizeof(Pt)*recs); NP=0;
    for(int r=0;r<recs;r++){
        float v[12];
        for(int j=0;j<12;j++) v[j]=F[r*NF+j];
        float x=v[ax],y=v[ay],z=v[az];
        if(!isfinite(x)||!isfinite(y)||!isfinite(z)) continue;
        if(fabsf(x)>1e4f||fabsf(y)>1e4f||fabsf(z)>1e4f) continue;
        PTS[NP].x=x;PTS[NP].y=y;PTS[NP].z=z;PTS[NP].used=1;NP++;
    }
    fprintf(stderr,"points: %d/%d (axes f%d,f%d,f%d)\n",NP,recs,ax,ay,az);
    if(!NP) return 2;

    /* center + scale */
    float mnx=1e30f,mny=1e30f,mnz=1e30f,mxx=-1e30f,mxy=-1e30f,mxz=-1e30f;
    for(int i=0;i<NP;i++){
        if(PTS[i].x<mnx)mnx=PTS[i].x; if(PTS[i].x>mxx)mxx=PTS[i].x;
        if(PTS[i].y<mny)mny=PTS[i].y; if(PTS[i].y>mxy)mxy=PTS[i].y;
        if(PTS[i].z<mnz)mnz=PTS[i].z; if(PTS[i].z>mxz)mxz=PTS[i].z;
    }
    float cx=(mnx+mxx)*0.5f, cy=(mny+mxy)*0.5f, cz=(mnz+mxz)*0.5f;
    float ext=mxx-mnx; if(mxy-mny>ext)ext=mxy-mny; if(mxz-mnz>ext)ext=mxz-mnz;
    if(ext<=0)ext=1;
    for(int i=0;i<NP;i++){
        PTS[i].x=(PTS[i].x-cx)/ext; PTS[i].y=(PTS[i].y-cy)/ext; PTS[i].z=(PTS[i].z-cz)/ext;
    }

    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    if(SDL_Init(SDL_INIT_VIDEO)!=0){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 3;}
    SDL_Window*win=SDL_CreateWindow("cmr2lab",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,W,H,0);
    if(!win){fprintf(stderr,"win: %s\n",SDL_GetError());return 4;}
    SDL_Renderer*ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE);
    if(!ren){fprintf(stderr,"ren: %s\n",SDL_GetError());return 5;}
    SDL_SetRenderDrawColor(ren,8,8,12,255); SDL_RenderClear(ren);

    Cam cam; memset(&cam,0,sizeof cam);
    cam.mode=0; cam.yaw=0.6f; cam.pitch=0.35f; cam.dist=2.2f; cam.fov=0.9f;
    Time tm; memset(&tm,0,sizeof tm); tm.last=1; tm.playing=1; tm.speed=1.0f;

    SDL_SetRenderDrawColor(ren,125,211,252,255);
    int drawn=0;
    for(int i=0;i<NP;i++){
        int ox,oy;
        if(project(&cam,W,H,PTS[i].x,PTS[i].y,PTS[i].z,&ox,&oy)){
            if(ox>=0&&ox<W&&oy>=0&&oy<H){ SDL_RenderDrawPoint(ren,ox,oy); drawn++; }
        }
    }
    fprintf(stderr,"drawn: %d\n",drawn);
    SDL_RenderPresent(ren);

    SDL_Surface*sur=SDL_CreateRGBSurfaceWithFormat(0,W,H,32,SDL_PIXELFORMAT_RGB24);
    SDL_RenderReadPixels(ren,NULL,SDL_PIXELFORMAT_RGB24,sur->pixels,sur->pitch);
    FILE*o=fopen(out,"wb");
    fprintf(o,"P6\n%d %d\n255\n",W,H);
    for(int y=0;y<H;y++) fwrite((Uint8*)sur->pixels+y*sur->pitch,1,W*3,o);
    fclose(o);
    fprintf(stderr,"wrote %s\n",out);

    SDL_FreeSurface(sur); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    free(F); free(PTS);
    return 0;
}
