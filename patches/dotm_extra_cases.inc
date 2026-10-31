
    case PPC_INST_VPKSWSS128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.s16, simde_mm_packs_epi32(simde_mm_load_si128((simde__m128i*){}.s32), simde_mm_load_si128((simde__m128i*){}.s32)));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;
    case PPC_INST_VPKUWUS128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u16, simde_mm_packus_epi32(simde_mm_min_epu32(simde_mm_load_si128((simde__m128i*){}.u32), simde_mm_set1_epi32(65535)), simde_mm_min_epu32(simde_mm_load_si128((simde__m128i*){}.u32), simde_mm_set1_epi32(65535))));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;
    case PPC_INST_VPKUHUS128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_packus_epi16(simde_mm_min_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_set1_epi16(255)), simde_mm_min_epu16(simde_mm_load_si128((simde__m128i*){}.u16), simde_mm_set1_epi16(255))));", v(insn.operands[0]), v(insn.operands[2]), v(insn.operands[1]));
        break;
    case PPC_INST_VCFPUXWS128:
    {
        printSetFlushMode(true);
        // Copy the source before writing any destination lane to permit vD == vB.
        println("\t{} = {};", vTemp(), v(insn.operands[1]));
        const uint32_t shift = uint32_t(insn.operands[2]);
        for (size_t lane = 0; lane < 4; ++lane)
        {
            println("\t{}.f64 = double({}.f32[{}]) * {};", temp(), vTemp(), lane, ldexp(1.0, shift));
            println("\t{}.u32[{}] = !({}.f64 > 0.0) ? 0u : {}.f64 >= 4294967295.0 ? 0xFFFFFFFFu : uint32_t({}.f64);",
                v(insn.operands[0]), lane, temp(), temp(), temp());
        }
        break;
    }
    case PPC_INST_VADDSWS:
    {
        println("\t{} = {};", vTemp(), v(insn.operands[1]));
        for (size_t lane = 0; lane < 4; ++lane)
        {
            println("\t{}.s64 = int64_t({}.s32[{}]) + int64_t({}.s32[{}]);",
                temp(), vTemp(), lane, v(insn.operands[2]), lane);
            println("\t{}.s32[{}] = {}.s64 > INT_MAX ? INT_MAX : ({}.s64 < INT_MIN ? INT_MIN : int32_t({}.s64));",
                v(insn.operands[0]), lane, temp(), temp(), temp());
        }
        break;
    }
    case PPC_INST_VSUBSHS:
        println("\tsimde_mm_store_si128((simde__m128i*){}.s16, simde_mm_subs_epi16(simde_mm_load_si128((simde__m128i*){}.s16), simde_mm_load_si128((simde__m128i*){}.s16)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;
    case PPC_INST_MULHD:
        println("\t{}.s64 = int64_t((__int128({}.s64) * __int128({}.s64)) >> 64);",
            r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        break;
    case PPC_INST_FRSQRTE:
        printSetFlushMode(false);
        println("\t{}.f64 = 1.0 / sqrt({}.f64);", f(insn.operands[0]), f(insn.operands[1]));
        break;
    case PPC_INST_LHBRX:
        print("\t{}.u64 = __builtin_bswap16(PPC_LOAD_U16(", r(insn.operands[0]));
        if (insn.operands[1] != 0)
            print("{}.u32 + ", r(insn.operands[1]));
        println("{}.u32));", r(insn.operands[2]));
        break;
    case PPC_INST_LVEHX:
        print("\t{} = ", ea());
        if (insn.operands[1] != 0)
            print("{}.u32 + ", r(insn.operands[1]));
        println("{}.u32;", r(insn.operands[2]));
        println("\t{}.u16[7 - (({} & 0xF) >> 1)] = PPC_LOAD_U16({} & ~1u);",
            v(insn.operands[0]), ea(), ea());
        break;
    case PPC_INST_DCBST:
        // The 360's ordinary cache-block store is not a guest-visible memory mutation.
        break;
