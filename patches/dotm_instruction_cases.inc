// DOTM-specific missing PowerPC instruction translations.
// Included inside Recompiler::Recompile(...), in the instruction switch.
// Runtime and memory macros are supplied by upstream XenonRecomp.
    case PPC_INST_BDZF:
    case PPC_INST_BDNZT:
    {
        const uint32_t bi = uint32_t(insn.operands[0]);
        const uint32_t target = uint32_t(insn.operands[1]);
        constexpr const char* fields[] = {"lt", "gt", "eq", "so"};
        const bool zero = id == PPC_INST_BDZF;
        println("\t--{}.u64;", ctr());
        print("\tif ({}.u32 {} 0 && {}{}.{})", ctr(), zero ? "==" : "!=",
              zero ? "!" : "", cr(bi / 4), fields[bi % 4]);
        if (target >= fn.base && target < fn.base + fn.size)
            println(" goto loc_{:X};", target);
        else
        {
            println(" {{");
            printFunctionCall(target);
            println("\treturn;");
            println("\t}}");
        }
        break;
    }
    case PPC_INST_STFSU:
    case PPC_INST_STFSUX:
    {
        printSetFlushMode(false);
        const bool indexed = id == PPC_INST_STFSUX;
        if (indexed)
            println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        else
            println("\t{} = {}.u32 + {};", ea(), r(insn.operands[2]), int32_t(insn.operands[1]));
        println("\t{}.f32 = float({}.f64);", temp(), f(insn.operands[0]));
        println("\tPPC_STORE_U32({}, {}.u32);", ea(), temp());
        println("\t{}.u32 = {};", r(insn.operands[indexed ? 1 : 2]), ea());
        break;
    }
    case PPC_INST_LFSU:
    case PPC_INST_LFSUX:
    {
        printSetFlushMode(false);
        const bool indexed = id == PPC_INST_LFSUX;
        if (indexed)
            println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        else
            println("\t{} = {}.u32 + {};", ea(), r(insn.operands[2]), int32_t(insn.operands[1]));
        println("\t{}.u32 = PPC_LOAD_U32({});", temp(), ea());
        println("\t{}.f64 = double({}.f32);", f(insn.operands[0]), temp());
        println("\t{}.u32 = {};", r(insn.operands[indexed ? 1 : 2]), ea());
        break;
    }
    case PPC_INST_STHU:
    case PPC_INST_STHUX:
    {
        const bool indexed = id == PPC_INST_STHUX;
        if (indexed)
            println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        else
            println("\t{} = {}.u32 + {};", ea(), r(insn.operands[2]), int32_t(insn.operands[1]));
        println("\tPPC_STORE_U16({}, {}.u16);", ea(), r(insn.operands[0]));
        println("\t{}.u32 = {};", r(insn.operands[indexed ? 1 : 2]), ea());
        break;
    }
    case PPC_INST_LHZU:
    case PPC_INST_LHZUX:
    {
        const bool indexed = id == PPC_INST_LHZUX;
        if (indexed)
            println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        else
            println("\t{} = {}.u32 + {};", ea(), r(insn.operands[2]), int32_t(insn.operands[1]));
        println("\t{}.u64 = PPC_LOAD_U16({});", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[indexed ? 1 : 2]), ea());
        break;
    }
    case PPC_INST_LHAU:
    {
        println("\t{} = {}.u32 + {};", ea(), r(insn.operands[2]), int32_t(insn.operands[1]));
        println("\t{}.s64 = int16_t(PPC_LOAD_U16({}));", r(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;
    }
    case PPC_INST_STFDU:
    case PPC_INST_LFDU:
    {
        printSetFlushMode(false);
        println("\t{} = {}.u32 + {};", ea(), r(insn.operands[2]), int32_t(insn.operands[1]));
        if (id == PPC_INST_STFDU)
            println("\tPPC_STORE_U64({}, {}.u64);", ea(), f(insn.operands[0]));
        else
            println("\t{}.u64 = PPC_LOAD_U64({});", f(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[2]), ea());
        break;
    }
    case PPC_INST_LFDUX:
    {
        printSetFlushMode(false);
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.u64 = PPC_LOAD_U64({});", f(insn.operands[0]), ea());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;
    }
    case PPC_INST_LWZUX:
    case PPC_INST_LBZUX:
    case PPC_INST_LDUX:
    {
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        const char* load = id == PPC_INST_LWZUX ? "PPC_LOAD_U32" :
                           id == PPC_INST_LBZUX ? "PPC_LOAD_U8" : "PPC_LOAD_U64";
        println("\t{}.u64 = {}({});", r(insn.operands[0]), load, ea());
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;
    }
    case PPC_INST_STBUX:
    {
        println("\t{} = {}.u32 + {}.u32;", ea(), r(insn.operands[1]), r(insn.operands[2]));
        println("\tPPC_STORE_U8({}, {}.u8);", ea(), r(insn.operands[0]));
        println("\t{}.u32 = {};", r(insn.operands[1]), ea());
        break;
    }
    case PPC_INST_EQV:
        println("\t{}.u64 = ~({}.u64 ^ {}.u64);", r(insn.operands[0]), r(insn.operands[1]), r(insn.operands[2]));
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;
    case PPC_INST_SUBFZE:
        println("\t{}.u64 = {}.u64;", temp(), r(insn.operands[1]));
        println("\t{}.u64 = ~{}.u64 + {}.ca;", r(insn.operands[0]), temp(), xer());
        println("\t{}.ca = ({}.u64 == 0 && {}.ca);", xer(), temp(), xer());
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;
    case PPC_INST_ADDME:
        println("\t{}.u64 = {}.u64;", temp(), r(insn.operands[1]));
        println("\t{}.u64 = {}.u64 - 1 + {}.ca;", r(insn.operands[0]), temp(), xer());
        println("\t{}.ca = ({}.u64 != 0 || {}.ca);", xer(), temp(), xer());
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;
    case PPC_INST_ADDC:
        println("\t{}.u64 = {}.u64 + {}.u64;", temp(), r(insn.operands[1]), r(insn.operands[2]));
        println("\t{}.ca = ({}.u64 < {}.u64);", xer(), temp(), r(insn.operands[1]));
        println("\t{}.u64 = {}.u64;", r(insn.operands[0]), temp());
        if (strchr(insn.opcode->name, '.'))
            println("\t{}.compare<int32_t>({}.s32, 0, {});", cr(0), r(insn.operands[0]), xer());
        break;
    case PPC_INST_VNOR128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_xor_si128(simde_mm_or_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)), simde_mm_set1_epi32(-1)));", v(insn.operands[0]), v(insn.operands[1]), v(insn.operands[2]));
        break;
    case PPC_INST_VSEL128:
        println("\tsimde_mm_store_si128((simde__m128i*){}.u8, simde_mm_or_si128(simde_mm_andnot_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8)), simde_mm_and_si128(simde_mm_load_si128((simde__m128i*){}.u8), simde_mm_load_si128((simde__m128i*){}.u8))));", v(insn.operands[0]), v(insn.operands[3]), v(insn.operands[1]), v(insn.operands[3]), v(insn.operands[2]));
        break;
