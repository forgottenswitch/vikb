#include "kl.h"
#include "ka.h"
#include "km.h"
#include "lm.h"
#include "scancodes.h"
#ifndef NOGUI
# include "ui.h"
#endif // NOGUI

bool KL_active = false;
HHOOK KL_handle;

KM KL_km_shift;
KM KL_km_control;
KM KL_km_alt;
KM KL_km_win;
KM KL_km_l3;
KM KL_km_l5;

KLY KL_kly;

UCHAR KL_phys[MAXSC];
UCHAR KL_phys_mods[MAXSC];

VK KL_mods_vks[MAXSC];

void KL_activate() {
    KL_handle = SetWindowsHookEx(WH_KEYBOARD_LL, KL_proc, OS_current_module_handle(), 0);
    if (KL_handle == nil) {
        dputs("\n\nSetWindowsHookEx failed!");
        OS_print_last_error();
        dputs("\n");
    }
    KL_active = true;
#ifndef NOGUI
    UI_TR_update();
#endif // NOGUI
}

void KL_deactivate() {
    UnhookWindowsHookEx(KL_handle);
    KL_active = false;
#ifndef NOGUI
    UI_TR_update();
#endif // NOGUI
}

void KL_toggle() {
    if (KL_active) {
        KL_deactivate();
    } else {
        KL_activate();
    }
}

#define RawThisEvent() 0
#define StopThisEvent() 1
#define PassThisEvent() CallNextHookEx(NULL, aCode, wParam, lParam)
LRESULT CALLBACK KL_proc(int aCode, WPARAM wParam, LPARAM lParam) {
    if (aCode != HC_ACTION)
        return PassThisEvent();
    PKBDLLHOOKSTRUCT ev = (PKBDLLHOOKSTRUCT) lParam;
    DWORD flags = ev->flags;
    SC sc = (SC) ev->scanCode;
    //VK vk = (VK) ev->vkCode;
    bool down = (wParam != WM_KEYUP && wParam != WM_SYSKEYUP);
    // non-physical key events:
    //   injected key presses (generated by programs - keybd_event(), SendInput()),
    //   their (non-injected) release counterparts,
    //   fake shift presses and releases by driver accompanying numpad keys
    //     (so that numlock'ed keys are independent of shift state yet have the same 2 levels),
    //   LControl press/release by OS (the window system) triggered by AltGr RAlt event
    // Only check here for injected presses and corresponding releases.
    bool faked;
    faked = (flags & LLKHF_INJECTED || (!(KL_phys[sc]) && !down));

    if (!faked) {
        KL_phys[sc] = down;
        BYTE mod = KL_phys_mods[sc];
        if (mod) {
            switch (mod) {
            case MOD_SHIFT:
                KM_shift_event(&KL_km_shift, down, sc);
                break;
            case MOD_CONTROL:
                KM_shift_event(&KL_km_control, down, sc);
                break;
            case MOD_ALT:
                KM_shift_event(&KL_km_alt, down, sc);
                break;
            case MOD_WIN:
                KM_shift_event(&KL_km_win, down, sc);
                break;
            case KLM_PHYS_TEMP:
                KL_phys_mods[sc] = 0;
                goto mods_end;
            }
            return PassThisEvent();
        } else {
            KM_nonmod_event(&KL_km_shift, down, sc);
            KM_nonmod_event(&KL_km_l3, down, sc);
        }
    }
    mods_end:

    if (flags & LLKHF_EXTENDED)
        sc |= 0x100;
    if (faked || sc >= KPN) {
        if (!faked) {
            dput("{sc%02lx,vk%02lx%c} ", ev->scanCode, ev->vkCode, (down ? '_' : '^'));
        }
        return PassThisEvent();
    }

    unsigned char lv = 0;
    if (KL_km_l3.in_effect) {
        lv = 2;
    } else if (KL_km_l5.in_effect) {
        lv = 4;
    }
    if (KL_km_shift.in_effect) {
        lv += 1;
    }

    if (lv <= 1 && (KL_km_alt.in_effect || KL_km_control.in_effect || KL_km_win.in_effect)) {
        VK vk = KL_mods_vks[sc];
        if (vk) {
            keybd_event(vk, 0, (down ? 0 : KEYEVENTF_KEYUP), 0);
            dput(" SendVK%c(%02x,'%c')", (down ? '_' : '^'), vk, vk);
            return StopThisEvent();
        }
    }

    LK lk = KL_kly[lv][sc];
    dput(" l%d,b%x", lv, lk.binding);
    //dput(" [sc%03x%c]l%d,b%x", sc, (down?'_':'^'), lv, lk.binding);

    if (!lk.active) {
        dput(" na%s", (down ? "_ " : "^\n"));
        if (lv < 2) {
            dput("[12]");
            return PassThisEvent();
        } else if (lv < 4) {
            dput("[34]");
            INPUT inp[5], *curinp = inp;
            char lctrl = (KL_phys[SC_LCONTROL] ? 0 : 1), lctrl1=-lctrl;
            char ralt = (KL_phys[SC_RMENU] ? 0 : 1), ralt1=-ralt;
            size_t inpl = lctrl*2 + ralt*2;
            if (!inpl) {
                return PassThisEvent();
            }
            inpl++;
            size_t i;
            DWORD time = GetTickCount();
            fori (i, 0, inpl) {
                bool down1;
                SC sc1;
                if (lctrl) {
                    down1 = lctrl > 0;
                    sc1 = SC_LCONTROL;
                    lctrl = 0;
                } else if (ralt) {
                    down1 = ralt > 0;
                    sc1 = SC_RMENU;
                    ralt = 0;
                } else {
                    lctrl = lctrl1;
                    ralt = ralt1;
                    sc1 = sc;
                    down1 = down;
                }
                curinp->type = INPUT_KEYBOARD;
                curinp->ki.wVk = 0;
                curinp->ki.dwFlags = KEYEVENTF_SCANCODE | (down1 ? 0 : KEYEVENTF_KEYUP);
                curinp->ki.dwExtraInfo = 0;
                curinp->ki.wScan = sc1;
                curinp->ki.time = time;
                curinp++;
            }
            SendInput(inpl, inp, sizeof(INPUT));
            return StopThisEvent();
        } else {
            dput("[56]");
            return StopThisEvent();
        }
    }

    UCHAR mods = lk.mods;
    if (mods == KLM_SC) {
        dput("%csc%02lx=%02x ", (down ? '_' : '^'), ev->scanCode, lk.binding);
        keybd_event(0, lk.binding, KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP), 0);
        return StopThisEvent();
    } else if (mods == KLM_WCHAR) {
        if (down) {
            WCHAR wc = lk.binding;
            dput(" send U+%04x ", wc);
            INPUT inp;
            inp.type = INPUT_KEYBOARD;
            inp.ki.wVk = 0;
            inp.ki.dwFlags = KEYEVENTF_UNICODE;
            inp.ki.dwExtraInfo = 0;
            inp.ki.wScan = wc;
            inp.ki.time = GetTickCount();
            SendInput(1, &inp, sizeof(INPUT));
        }
    } else if (mods & KLM_KA) {
        dput(" ka_call ka%d(%d){", lk.binding, down);
        KA_call(lk.binding, down, sc);
        dput("}%s", (down ? "_" : "^\n"));
    } else {
        bool shift_was_down = KL_km_shift.in_effect;
        bool need_shift = (mods & MOD_SHIFT);
        char mod_shift = (KL_km_shift.in_effect ? (need_shift ? 0 : -1) : (need_shift ? 1 : 0)), mod_shift0 = mod_shift;
        char mod_control = (((mods & MOD_CONTROL) && !KL_km_control.in_effect) ? 1 : 0), mod_control0 = mod_control;
        char mod_alt = (((mods & MOD_ALT) && !KL_km_alt.in_effect) ? 1 : 0), mod_alt0 = mod_alt;
        int mods_count = (mod_shift & 1) + mod_control + mod_alt;
        if (!mods_count) {
            dput(" evt vk%02x%c", lk.binding, (down ? '_' : '^'));
            keybd_event(lk.binding, sc, (down ? 0 : KEYEVENTF_KEYUP), 0);
        } else {
            int inps_count = 1 + mods_count * 2;
            INPUT inps[7];
            int i, tick_count = GetTickCount();
            dput(" send%d vk%02x[%d%d%d]%s", inps_count, lk.binding, mod_shift, mod_control, mod_alt, (down ? "_" : "^\n"));
            fori (i, 0, inps_count) {
                VK vk1 = lk.binding;
                DWORD flags = 0;
                if (mod_shift) {
                    if (mod_shift > 0 && !need_shift && !shift_was_down) {
                        inps_count--;
                        continue;
                    }
                    dput("+%d-", mod_shift);
                    flags = (mod_shift > 0 ? 0 : KEYEVENTF_KEYUP);
                    mod_shift = 0;
                    vk1 = VK_LSHIFT;
                } else if (mod_control) {
                    dput("^");
                    flags = (mod_control > 0 ? 0 : KEYEVENTF_KEYUP);
                    mod_control = 0;
                    vk1 = VK_LCONTROL;
                } else if (mod_alt) {
                    dput("!");
                    flags = (mod_alt > 0 ? 0 : KEYEVENTF_KEYUP);
                    mod_alt = 0;
                    vk1 = VK_RMENU;
                } else {
                    dput("-");
                    mod_shift = -mod_shift0;
                    mod_control = -mod_control0;
                    mod_alt = -mod_alt0;
                    vk1 = lk.binding;
                    flags = (down ? 0 : KEYEVENTF_KEYUP);
                }
                INPUT *inp = &(inps[i]);
                inp->type = INPUT_KEYBOARD;
                inp->ki.wVk = vk1;
                inp->ki.dwFlags = flags;
                inp->ki.dwExtraInfo = 0;
                inp->ki.wScan = sc;
                inp->ki.time = tick_count;
            }
            SendInput(inps_count, inps, sizeof(INPUT));
        }
    }

    return StopThisEvent();
}
#undef RawThisEvent
#undef StopThisEvent
#undef PassThisEvent

KLY *KL_bind_kly = &KL_kly;

void KL_bind(SC sc, UINT lvl, UINT mods, SC binding) {
    LK lk;
    SC binding1 = binding;
    UINT lvl1 = lvl+1;
    if (mods & KLM_SC) {
        binding1 = OS_sc_to_vk(binding);
    }
    dput("bind sc%03x[%d]:", sc, lvl1);
    if (mods & KLM_WCHAR) {
        dput("u%04x ", binding);
    } else if (mods & KLM_KA) {
        dput("ka%d ", binding);
    } else {
        if (mods & KLM_SC) {
            dput("sc%03x=>vk%02x ", binding, binding1);
        } else {
            dput("vk%02x ", binding);
        }
    }
    if (!(lvl1 % 2) && !(mods & KLM_WCHAR) && !(mods & KLM_KA)) {
        dput("+(%x)", mods);
        mods |= MOD_SHIFT;
    }
    lk.active = true;
    lk.mods = mods;
    lk.binding = binding1;
    (*KL_bind_kly)[lvl][sc] = lk;
}

void KL_temp_sc(SC sc, SC mods, SC binding) {
    if (mods == KLM_KA) {
        dput("t sc%02x=ka%d ", sc, binding);
    } else if (mods == KLM_SC) {
        dput("t sc%02x=%02x ", sc, binding);
    } else {
        dput("t sc%02x={%02x/%02x} ", sc, binding, mods);
    }
    LK lk = { true, (UCHAR)mods, binding };
    KL_kly[0][sc] = lk;
    KL_kly[1][sc] = lk;
}

typedef struct {
    bool compiled;
    // Primary Language ID
    LANGID lang;
    // Whether defines VKs for while modifiers are in effect
    bool vks_lang;
    // Bindings
    KLY kly;
} KLC;

size_t KL_klcs_size = 0;
size_t KL_klcs_count = 0;
KLC *KL_klcs = (KLC*)calloc((KL_klcs_size = 4), sizeof(KLC));

void KL_add_lang(LANGID lang) {
    if ((KL_klcs_count+=1) > KL_klcs_size) {
        KL_klcs = (KLC*)realloc(KL_klcs, (KL_klcs_size *= 1.5) * sizeof(KLC));
    }
    KLC *klc = KL_klcs + KL_klcs_count - 1;
    klc->compiled = false;
    klc->lang = lang;
    klc->vks_lang = false;
}

KLY *KL_lang_to_kly(LANGID lang) {
    UINT i;
    fori(i, 0, KL_klcs_count) {
        if (KL_klcs[i].lang == lang)
            return &(KL_klcs[i].kly);
    }
    return nil;
}

KLC *KL_lang_to_klc(LANGID lang) {
    UINT i;
    dput("klcs_count:%d ", KL_klcs_count);
    fori(i, 0, KL_klcs_count) {
        dput("klc.lang:%x ", KL_klcs[i].lang);
        if (KL_klcs[i].lang == lang)
            return &(KL_klcs[i]);
    }
    return nil;
}

LANGID KL_vks_lang = LANG_NEUTRAL;

void KL_compile_klc(KLC *klc) {
    if (klc->lang == LANG_NEUTRAL)
        return;
    HKL cur_hkl = GetKeyboardLayout(0);
    ActivateKeyboardLayout(LM_langid_to_hkl(klc->lang), 0);
    KLY *kly = &(klc->kly);
    CopyMemory(KL_kly, KL_lang_to_kly(LANG_NEUTRAL), sizeof(KLY));
    bool vks_lang = klc->vks_lang;
    int lv, sc;
    fori(lv, 0, KLVN) {
        fori(sc, 0, KPN) {
            LK lk = (*kly)[lv][sc];
            if (lk.active) {
                dput("sc%03x:%d ", sc, lv+1);
                KL_kly[lv][sc] = lk;
            }
        }
    }
    fori (lv, 0, KLVN) {
        fori (sc, 0, KPN) {
            LK *p_lk = &(KL_kly[lv][sc]), lk = *p_lk;
            if (lk.active) {
                //dput("a(%03x:%d:%x/%x)", sc, lv+1, lk.binding, lk.mods);
                if (lk.mods & KLM_WCHAR) {
                    WCHAR w = lk.binding;
                    dput("sc%03x'%c':%d->", sc, w, lv+1);
                    KP kp = OS_wchar_to_vk(w);
                    if (kp.vk != 0xFF) {
                        dput("vk%02x/%d", kp.vk, kp.mods);
                        bool same_sc = (kp.mods == KLM_SC && kp.sc == sc);
                        bool same_vk = (lv <= 1 && kp.mods == (MOD_SHIFT * (lv % 2)) && kp.vk == OS_sc_to_vk(sc));
                        if (same_sc || same_vk) {
                            lk.active = 0;
                            lk.binding = 0;
                            lk.mods = 0;
                        } else {
                            if (lv == 0 && vks_lang && !(kp.mods & KLM_SC)) {
                                dput("_M");
                                KL_mods_vks[sc] = kp.vk;
                            }
                            lk.mods = kp.mods;
                            lk.binding = kp.vk;
                        }
                    } else {
                        dput("u%04x", w);
                    }
                    dput(";");
                }
                *p_lk = lk;
            }
        }
    }
    CopyMemory(kly, KL_kly, sizeof(KLY));
    klc->compiled = true;
    ActivateKeyboardLayout(cur_hkl, 0);
}

void KL_activate_lang(LANGID lang) {
    dput("lang %04x ", lang);
    KLC *lang_klc = KL_lang_to_klc(lang);
    if (lang_klc == nil) {
        dput("no such lang! ");
        return;
    }
    if (lang_klc->compiled) {
        CopyMemory(KL_kly, lang_klc->kly, sizeof(KLY));
    } else {
        dput("compile ");
        KL_compile_klc(lang_klc);
    }
    dputs("");
}

void KL_set_bind_lang(LANGID lang) {
    KLY *kly = KL_lang_to_kly(lang);
    if (kly == nil) {
        KL_add_lang(lang);
        kly = KL_lang_to_kly(lang);
    }
    KL_bind_kly = kly;
}

void KL_set_vks_lang(LANGID lang) {
    KL_vks_lang = lang;
}

void KL_define_one_vk(VK vk, KLY *kly) {
    SC sc = OS_vk_to_sc(vk);
    if (!sc) {
        return;
    }
    LK lk = (*kly)[0][sc];
    if (lk.active && lk.mods == 0) {
        vk = lk.binding;
    }
    dput(" DefineVK(%02x, sc%03x)", vk, sc);
    KL_mods_vks[sc] = vk;
}

void KL_define_vks() {
    KLC *klc = KL_lang_to_klc(KL_vks_lang);
    KLY *kly = &(klc->kly);
    klc->vks_lang = true;
    if (!klc->compiled) {
        KL_compile_klc(klc);
    }
    int vk;
    // 0..9
    fori (vk, 0x30, 0x3A) {
        KL_define_one_vk(vk, kly);
    }
    // A..Z
    fori (vk, 0x41, 0x5B) {
        KL_define_one_vk(vk, kly);
    }
    // Tilde, comma, period, brackets, semicolon, quote, slash
    KL_define_one_vk(VK_OEM_1, kly);
    KL_define_one_vk(VK_OEM_2, kly);
    KL_define_one_vk(VK_OEM_3, kly);
    KL_define_one_vk(VK_OEM_4, kly);
    KL_define_one_vk(VK_OEM_5, kly);
    KL_define_one_vk(VK_OEM_6, kly);
    KL_define_one_vk(VK_OEM_7, kly);
    KL_define_one_vk(VK_OEM_8, kly);
}

void KL_bind_init() {
    KL_bind_kly = KL_lang_to_kly(LANG_NEUTRAL);
}

void KL_init() {
    ZeroBuf(KL_kly);
    ZeroBuf(KL_phys);

    KM_init(&KL_km_shift);
    KM_init(&KL_km_control);
    KM_init(&KL_km_alt);
    KM_init(&KL_km_l3);
    KM_init(&KL_km_l5);

    KL_add_lang(LANG_NEUTRAL);
    KL_bind_kly = KL_lang_to_kly(LANG_NEUTRAL);
    UINT sc;
    fori (sc, 0, len(KL_phys_mods)) {
        VK vk = OS_sc_to_vk(sc);
        UCHAR mod = 0;
        switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
            mod = MOD_SHIFT;
            break;
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
            mod = MOD_CONTROL;
            break;
        case VK_MENU: case VK_LMENU: case VK_RMENU:
            mod = MOD_ALT;
            break;
        case VK_LWIN: case VK_RWIN:
            mod = MOD_WIN;
            break;
        }
        if (mod) {
            printf("[mods] sc%03x => vk%02x, mod %x\t", sc, vk, mod);
        }
        KL_phys_mods[sc] = mod;
    }
#define mod_vk(mod, vk) do { sc = OS_vk_to_sc(vk); if (sc) { printf("[mods] sc%03x => vk%02x, mod %x\t", sc, vk, mod); KL_phys_mods[sc] = mod; }; } while (0)
    mod_vk(MOD_WIN, VK_LWIN);
    mod_vk(MOD_WIN, VK_RWIN);
#undef mod_vk
}