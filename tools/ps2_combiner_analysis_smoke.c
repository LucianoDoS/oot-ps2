/* Cache identity checks against the original single-analysis path. */
#include <stdio.h>
#include <string.h>
#include "rdp_to_gs.h"
#include "ps2_gfx_profile.h"

typedef struct {
    u32 prim, env, primMasks[4], envMasks[4];
    u8 flags[8], color[4], vertex[4];
} Result;
static void result(const RDPState* rdp, Result* out) {
    const u8 texel[4]={7,31,211,239}, shade[4]={119,71,87,255};
    memset(out,0,sizeof(*out));
    out->flags[0]=ps2_combiner_uses_shade(rdp,false);
    out->flags[1]=ps2_combiner_uses_shade(rdp,true);
    out->flags[2]=ps2_combiner_texture_lerp(rdp);
    out->flags[3]=ps2_combiner_path_lerp(rdp);
    out->flags[4]=ps2_combiner_dynamic_alpha(rdp);
    out->flags[5]=ps2_combiner_flame(rdp);
    out->flags[6]=ps2_combiner_token_flame(rdp);
    out->flags[7]=ps2_combiner_jewel(rdp);
    ps2_combiner_constants(rdp,&out->prim,&out->env);
    ps2_combiner_lookup_dependencies(rdp,out->primMasks,out->envMasks);
    ps2_combiner_color(rdp,texel,shade,out->color);
    ps2_combiner_vertex_color(rdp,shade,out->vertex);
}
int ps2_combiner_analysis_smoke(void) {
    static RDPState materials[80];
    const u32 words[][2]={{0,0},{0xfc119623,0xff2fffff},{0xfc262a04,0x1f5893f8},
        {0xfc267e04,0x1ffcfdf8},{0xfcffffff,0xfffe793c},{0xfc121824,0xff33ffff},
        {0xfc309661,0x552eff7f},{0xfc309661,0x552eff38}};
    unsigned failures=0, cases=0;
    u32 random=0x71357209u;
    for (unsigned i=0;i<80;i++) {
        RDPState* rdp=&materials[i]; memset(rdp,0,sizeof(*rdp));
        random=random*1664525u+1013904223u;
        rdp->combiner.w0=i<8?words[i][0]:0xfc000000u|(random&0xffffff);
        random=random*1664525u+1013904223u;
        rdp->combiner.w1=i<8?words[i][1]:random;
        rdp->otherMode.hi=(i<8 ? (i>=2 ? 1u : 0u) : (i&3))<<20;
        rdp->otherMode.lo=((i&1)?0x1000:0)|((i&2)?0x4000:0)|((i>>2)&3);
        if (i==2 || i==3) rdp->otherMode.lo=0;
        rdp->primColor=0x12587df3u+i*0x413509u; rdp->envColor=0x861153f9u-i*0x71537u;
    }
    ps2_combiner_test_analysis_clear();
    Result reference, cached;
    for (unsigned pass=0;pass<6;pass++) for (unsigned i=0;i<80;i++) {
        RDPState rdp=materials[(i*17+pass*23)%80]; /* pressure, collisions and restored snapshots */
        rdp.primColor^=pass*0x27431u; rdp.envColor^=pass*0x798a71u; rdp.primLodFrac=pass*47;
        ps2_combiner_test_analysis_reference(true); result(&rdp,&reference);
        ps2_combiner_test_analysis_reference(false); result(&rdp,&cached);
        failures+=memcmp(&reference,&cached,sizeof(reference))!=0; cases++;
    }
    /* Revisit eight materials with different tint values. Selector analyses
     * are independent of those values, but the resulting colors are not. */
    u32 cycles[2], checksum[2]={0};
    PS2CombinerAnalysisStats counts[2];
    bool previousActive=gPS2GfxProfileActive;
    for (unsigned path=0;path<2;path++) {
        ps2_combiner_test_analysis_clear(); ps2_combiner_test_analysis_reference(!path);
        gPS2GfxProfileActive=false;
        u32 start=ps2_gfx_cycles();
        for (unsigned i=0;i<2048;i++) {
            RDPState* rdp=&materials[i&7];
            u32 prim,env; ps2_combiner_constants(rdp,&prim,&env);
            checksum[path]+=prim^env;
            checksum[path]+=ps2_combiner_uses_shade(rdp,false)+ps2_combiner_dynamic_alpha(rdp);
        }
        cycles[path]=ps2_gfx_cycles()-start;
        ps2_combiner_test_analysis_clear(); gPS2GfxProfileActive=true;
        for (unsigned i=0;i<256;i++) result(&materials[i&7],&cached);
        ps2_combiner_analysis_get_stats(&counts[path]);
        failures+=counts[path].lookups!=counts[path].lastHits+counts[path].savedHits+counts[path].rebuilds;
    }
    failures+=checksum[0]!=checksum[1] || counts[0].rebuilds!=256 ||
        counts[1].rebuilds>=counts[0].rebuilds || !counts[1].savedHits;
    gPS2GfxProfileActive=previousActive;
    ps2_combiner_test_analysis_reference(false); ps2_combiner_test_analysis_clear();
    printf("[PS2 COMBINER ANALYSIS TEST] cases=%u failures=%u referenceCycles=%u cachedCycles=%u referenceBuilds=%u cachedBuilds=%u savedHits=%u storageBytes=%u\n",
        cases,failures,(unsigned)cycles[0],(unsigned)cycles[1],(unsigned)counts[0].rebuilds,
        (unsigned)counts[1].rebuilds,(unsigned)counts[1].savedHits,(unsigned)counts[1].storageBytes);
    return failures;
}
