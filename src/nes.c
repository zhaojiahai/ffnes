// 包含头文件
#include "vdev.h"
#include "nes.h"
#include "log.h"
#include "save.h"

// 内部函数实现
static void nes_do_reset(NES *nes)
{
    // mmc need reset first
    mmc_reset(&(nes->mmc));

    // reset cpu & ppu & apu
    cpu_reset(&(nes->cpu));
    ppu_reset(&(nes->ppu));
    apu_reset(&(nes->apu));

    // reset joypad
    joypad_reset(&(nes->pad));

    // reset replay
    replay_reset(&(nes->replay));

    // restart ndb
    ndb_set_debug(&(nes->ndb), NDB_DEBUG_MODE_RESTART);
}

static void* nes_thread_proc(void *param)
{
    NES *nes = (NES*)param;
    int totalpclk, nmi, irq;

    while (!nes->thread_exit)
    {
        //++ for run/pause ++//
        if (!nes->isrunning)
        {
            nes->ispaused = 1;
            nes->ppu.vdev->bufrequest(nes->ppu.vctxt, NULL, NULL);
            nes->ppu.vdev->bufpost   (nes->ppu.vctxt);
            continue;
        }
        //-- for run/pause --//

        //++ for replay playing ++//
        if (!replay_progress(&(nes->replay)) && nes->request_reset == 0)
        {
            nes->ppu.vdev->bufrequest(nes->ppu.vctxt, NULL, NULL);
            nes->ppu.vdev->bufpost   (nes->ppu.vctxt);
            continue;
        }
        //-- for replay playing --//

        //++ for nes reset ++//
        if (nes->request_reset == 1)
        {
            nes->request_reset = 0;
            nes_do_reset(nes);
        }
        //-- for nes reset --//

        //++ run cpu & apu & ppu
        totalpclk = nes->ppu.oddevenflag ? NES_HTOTAL * NES_VTOTAL - 1 : NES_HTOTAL * NES_VTOTAL;
        do {
            cpu_run_pclk(&(nes->cpu)); // run cpu
            ppu_run_pclk(&(nes->ppu)); // run ppu
            apu_run_pclk(&(nes->apu)); // run apu

            //+ for cpu nmi & irq
            nmi =   nes->ppu.pinvbl;
            irq = !(nes->apu.regs[0x0015] & 0xC0);
            cpu_nmi(&(nes->cpu), nmi);
            cpu_irq(&(nes->cpu), irq);
            //- for cpu nmi & irq
        } while (--totalpclk > 0);
        //-- run cpu & apu & ppu

        // run joypad for turbo key function
        joypad_run(&(nes->pad));
    }
    return NULL;
}

// 函数实现
BOOL nes_init(NES *nes, char *file, DWORD extra)
{
    int *mirroring = NULL;
    int  i         = 0;

    log_init("DEBUGER"); // log init

    // clear it
    memset(nes, 0, sizeof(NES));

    // extra data
    nes->extra = extra;

    // load cartridge first
    if (!cartridge_load(&(nes->cart), file))
    {
        log_printf("failed to load nes rom file !\n");
    }

    //++ cbus mem map ++//
    // create cpu ram
    nes->cram.type = MEM_RAM;
    nes->cram.size = NES_CRAM_SIZE;
    nes->cram.data = nes->cpu.cram;

    // create ppu regs
    nes->ppuregs.type = MEM_REG;
    nes->ppuregs.size = NES_PPUREGS_SIZE;
    nes->ppuregs.data = nes->ppu.regs;
    nes->ppuregs.r_callback = NES_PPU_REG_RCB;
    nes->ppuregs.w_callback = NES_PPU_REG_WCB;

    // create apu regs
    nes->apuregs.type = MEM_REG;
    nes->apuregs.size = NES_APUREGS_SIZE;
    nes->apuregs.data = nes->apu.regs;
    nes->apuregs.r_callback = NES_APU_REG_RCB;
    nes->apuregs.w_callback = NES_APU_REG_WCB;

    // create expansion rom
    nes->erom.type = MEM_ROM;
    nes->erom.size = NES_EROM_SIZE;
    nes->erom.data = nes->buf_erom;

    // create sram
    nes->sram.type = MEM_RAM;
    nes->sram.size = NES_SRAM_SIZE;
    nes->sram.data = nes->cart.buf_sram;

    // create PRG-ROM 0
    nes->prom0.type = MEM_ROM;
    nes->prom0.size = NES_PRGROM_SIZE;

    // create PRG-ROM 1
    nes->prom1.type = MEM_ROM;
    nes->prom1.size = NES_PRGROM_SIZE;

    // init nes cbus
    bus_setmem(nes->cbus, 0, 0xC000, 0xFFFF, &(nes->prom1  ));
    bus_setmem(nes->cbus, 1, 0x8000, 0xBFFF, &(nes->prom0  ));
    bus_setmem(nes->cbus, 2, 0x6000, 0x7FFF, &(nes->sram   ));
    bus_setmem(nes->cbus, 3, 0x4020, 0x5FFF, &(nes->erom   ));
    bus_setmem(nes->cbus, 4, 0x4000, 0x401F, &(nes->apuregs));
    bus_setmem(nes->cbus, 5, 0x2000, 0x3FFF, &(nes->ppuregs));
    bus_setmem(nes->cbus, 6, 0x0000, 0x1FFF, &(nes->cram   ));
    //-- cbus mem map --//

    //++ pbus mem map ++//
    // create CHR-ROM0
    nes->chrrom0.type = MEM_ROM;
    nes->chrrom0.size = NES_CHRROM_SIZE;

    // create CHR-ROM1
    nes->chrrom1.type = MEM_ROM;
    nes->chrrom1.size = NES_CHRROM_SIZE;

    // create vram
    mirroring = cartridge_get_vram_mirroring(&(nes->cart));
    for (i=0; i<4; i++)
    {
        nes->vram[i].type = MEM_RAM;
        nes->vram[i].size = NES_VRAM_SIZE;
        nes->vram[i].data = nes->buf_vram[mirroring[i]];
    }

    // create color palette
    nes->palette.type = MEM_RAM;
    nes->palette.size = NES_PALETTE_SIZE;
    nes->palette.data = nes->ppu.palette;

    // init nes pbus
    bus_setmir(nes->pbus, 0, 0x3F20, 0x3FFF, 0x3F1F);
    bus_setmem(nes->pbus, 1, 0x3F00, 0x3F1F, &(nes->palette));
    bus_setmir(nes->pbus, 2, 0x3000, 0x3EFF, 0x2FFF);
    bus_setmem(nes->pbus, 3, 0x2C00, 0x2FFF, &(nes->vram[3]));
    bus_setmem(nes->pbus, 4, 0x2800, 0x2BFF, &(nes->vram[2]));
    bus_setmem(nes->pbus, 5, 0x2400, 0x27FF, &(nes->vram[1]));
    bus_setmem(nes->pbus, 6, 0x2000, 0x23FF, &(nes->vram[0]));
    bus_setmem(nes->pbus, 7, 0x1000, 0x1FFF, &(nes->chrrom1));
    bus_setmem(nes->pbus, 8, 0x0000, 0x0FFF, &(nes->chrrom0));
    //-- pbus mem map --//

    // init mmc before cpu & ppu & apu, due to mmc will do bank switch
    // this will change memory mapping on cbus & pbus
    mmc_init(&(nes->mmc), &(nes->cart), nes->cbus, nes->pbus);

    // now it's time to init cpu & ppu & apu
    cpu_init(&(nes->cpu), nes->cbus );
    ppu_init(&(nes->ppu), nes->extra, NULL);
    apu_init(&(nes->apu), nes->extra, NULL);
    ndb_init(&(nes->ndb), nes       );

    // init joypad
    joypad_init  (&(nes->pad));
    joypad_setkey(&(nes->pad), 0, NES_PAD_CONNECT, 1);
    joypad_setkey(&(nes->pad), 1, NES_PAD_CONNECT, 1);

    // init replay
    replay_init(&(nes->replay));

    // create nes event & thread
    pthread_create(&(nes->thread_id), NULL, nes_thread_proc, nes);

    return TRUE;
}

void nes_free(NES *nes)
{
    // disable ndb debugging will make cpu keep running
    ndb_set_debug(&(nes->ndb), NDB_DEBUG_MODE_DISABLE);

    // destroy nes thread
    nes->thread_exit = TRUE;
    pthread_join(nes->thread_id, NULL);

    // free replay
    replay_free(&(nes->replay));

    // free joypad
    joypad_setkey(&(nes->pad), 0, NES_PAD_CONNECT, 0);
    joypad_setkey(&(nes->pad), 1, NES_PAD_CONNECT, 0);
    joypad_free  (&(nes->pad ));

    // free cpu & ppu & apu & mmc
    cpu_free(&(nes->cpu));
    ppu_free(&(nes->ppu));
    apu_free(&(nes->apu));
    mmc_free(&(nes->mmc));
    ndb_free(&(nes->ndb));

    // free cartridge
    cartridge_free(&(nes->cart));

    log_done(); // log done
}

void nes_reset(NES *nes)
{
    // disable ndb debugging will make cpu keep running
    ndb_set_debug(&(nes->ndb), NDB_DEBUG_MODE_DISABLE);
    nes->request_reset = 1; // request reset
    nes_textout(nes, 0, 222, "reset", 2000, 1);
}

void nes_setrun(NES *nes, int run)
{
    if (run) { nes_textout(nes, 0, 222, "running", 2000, 1); }
    else     { nes_textout(nes, 0, 222, "paused" , -1  , 1); Sleep(16); }
    nes->isrunning = run;
    nes->ispaused  = 0;
    if (!run) while (!nes->ispaused) Sleep(1);
}

int nes_getrun(NES *nes)
{
    return nes->isrunning;
}

void nes_joypad(NES *nes, int pad, int key, int value)
{
    joypad_setkey(&(nes->pad), pad, key, value);
}

void nes_textout(NES *nes, int x, int y, char *text, int time, int priority)
{
    nes->ppu.vdev->textout(nes->ppu.vctxt, x, y, text, time, priority);
}

void nes_save_game  (NES *nes, char *file) { saver_save_game  (nes, file); }
void nes_load_game  (NES *nes, char *file) { saver_load_game  (nes, file); }
void nes_load_replay(NES *nes, char *file) { saver_load_replay(nes, file); }
int  nes_getfullscreen(NES *nes)           { return nes->ppu.vdev->getfullscreen(nes->ppu.vctxt); }
void nes_setfullscreen(NES *nes, int mode) { nes->ppu.vdev->setfullscreen(nes->ppu.vctxt, mode);  }

