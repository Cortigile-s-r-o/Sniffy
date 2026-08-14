#include "stlinkinspector.h"
#include <algorithm>

#include "stlink.h"
extern "C"
{
#include "chipid.h"
#include "read_write.h"
}

StLinkInspector::StLinkInspector(QObject *parent) : QObject(parent)
{
}

QString StLinkInspector::getDetectedMcu(stlink_t* stlink)
{
    if (!stlink)
    {
        return "Unknown";
    }

    // 1. Determine Family (e.g. F303)
    QString family = "Unknown";
    QChar packageChar = '?';
    QChar flashChar = '?';
    struct stlink_chipid_params *params = stlink_chipid_get_params(stlink->chip_id);
    if (params && params->dev_type)
    {
        QString devType = QString::fromLatin1(params->dev_type);
        // Expected format: STM32F303_High_Density or STM32F446 etc.
        // We want to extract "F303" or "F446"
        // Remove "STM32" prefix if present
        if (devType.startsWith("STM32"))
        {
            devType = devType.mid(5);
        }

        // Extract the Fxxx part.
        // Sometimes dev_type is "F303_High_Density" or just "F446"
        // We take characters until we hit a non-alphanumeric or underscore
        int end = 0;
        while (end < devType.length() && (devType[end].isLetterOrNumber()))
        {
            end++;
        }
        family = devType.left(end);
    }
    else
    {
        // Fallback based on Chip ID if params not found
        // This is a partial list, can be expanded

        switch (stlink->chip_id)
        {
        case 0x410:
            family = "F103";
            packageChar = 'R';
            flashChar = 'B';
            break; // F1 medium density (STM32F103C8/R8/RB)
        case 0x446:
            family = "F303";
            packageChar = 'R';
            flashChar = 'E';
            break; // F3 High Density (STM32F303xD/xE)
        case 0x422:
            family = "F303";
            break; // F303xB/xC (Medium Density)
        case 0x438:
            family = "F303";
            packageChar = 'K';
            flashChar = '8';
            break;  // F303x6/x8 (Small Density)
        case 0x411: // <-- FIXED (Was 0x413)
            family = "F405";
            break;  // F405/407/415/417 High Performance
        case 0x413: // <-- ADDED FOR ACCURACY
            family = "F401";
            break; // F401 Access Line
        case 0x419:
            family = "F427";
            break; // F427/437/429/439
        case 0x421:
            family = "F446";
            packageChar = 'R';
            flashChar = 'E';
            break; // F446 Retarget
        case 0x431:
            family = "F411";
            packageChar = 'R';
            flashChar = 'E';
            break;
        case 0x449:
            family = "F767";
            packageChar = 'Z';
            flashChar = 'I';
            break; // F76x/F77x Line
        case 0x443:
            family = "C071";
            packageChar = 'R';
            flashChar = 'B';
            break; // C051/C071 Line
        case 0x453:
            family = "C092";
            packageChar = 'R';
            flashChar = 'C';
            break;  // C011/C031/C091/C092 Line
        case 0x44E: // RM0522 DBGMCU_IDCODE DEV_ID for STM32C55x/C56x family (covers Nucleo-C562RE)
            family = "C562";
            packageChar = 'R';
            flashChar = 'E';
            break; // STM32C55x/C56x Mainstream Line (Cortex-M33 @ 144MHz)
        case 0x460:
            family = "G071";
            packageChar = 'R';
            flashChar = 'B';
            break; // G07x/G08x Line
        case 0x467:
            family = "G0B1";
            packageChar = 'R';
            flashChar = 'E';
            break; // G0Bx/G0Cx Line
        case 0x469:
            family = "G474";
            packageChar = 'R';
            flashChar = 'E';
            break; // G47x/G48x Hi-Res Line
        case 0x468:
            family = "G431";
            packageChar = 'R';
            flashChar = 'B';
            break; // G431/G441 Line
        case 0x470:
            family = "L432";
            packageChar = 'K';
            flashChar = 'C';
            break; // L43x/L44x Line
        case 0x415:
            family = "L476";
            packageChar = 'R';
            flashChar = 'G';
            break; // L47x/L48x Line (Nucleo-L476RG)
        case 0x474:
            family = "H503";
            packageChar = 'R';
            flashChar = 'B';
            break; // H503 Value Line (Note: Shares ID with F72x/73x)
        case 0x484:
            family = "H563";
            packageChar = 'Z';
            flashChar = 'I';
            break;  // H56x/H57x System Line
        case 0x450: // <-- FIXED COMMENT (Was labeled H763)
            family = "H743";
            packageChar = 'Z';
            flashChar = 'I';
            break;  // H74x/H75x Dual-Bank Line
        case 0x483: // <-- ADDED FOR H763
            family = "H763";
            packageChar = 'Z';
            flashChar = 'I';
            break; // True H76x/H77x Bootflash Line
        case 0x482:
            family = "U575";
            packageChar = 'Z';
            flashChar = 'I';
            break; // U575/U585 Ultra Low Power
        default:
            family = QString("UnknownID_%1").arg(stlink->chip_id, 0, 16);
            break;
        }
    }
    
    return QString("%1%2%3").arg(family).arg(packageChar).arg(flashChar);
}

void StLinkInspector::readUID(stlink_t* stlink)
{
    if (!stlink)
    {
        emit deviceUIDError("Device not connected");
        return;
    }

    // Ensure target is responsive
    stlink_force_debug(stlink);

    // Select family-specific UID base from loaded device parameters.
    // U5 chips expose their unique UID within the system memory area and are
    // not covered by the older F/G family code paths.
    // NOTE: this vendored libstlink (see THIRD-PARTY-NOTICES.md, v1.8.0) predates
    // upstream's STM32C5 support, so stlink->flash_type only ever takes the values
    // declared in libs/stlink/include/stm32.h below - it will never be C5, and F1/F0_F3
    // are reported combined as F0_F1_F3, L5/U5/H5 combined as L5_U5_H5.
    uint32_t uidBase = 0;
    switch (stlink->flash_type)
    {
    case STM32_FLASH_TYPE_C0:
    case STM32_FLASH_TYPE_G0:
    case STM32_FLASH_TYPE_G4:
    case STM32_FLASH_TYPE_L4:
        uidBase = 0x1FFF7590u; // STM32C0, G0, G4, L4/L4+ UID base
        break;

    case STM32_FLASH_TYPE_F2_F4:
        uidBase = 0x1FFF7A10u; // STM32F2 / STM32F4x UID base
        break;

    case STM32_FLASH_TYPE_F0_F1_F3:
        uidBase = (stlink->chip_id == 0x410 || stlink->chip_id == 0x412 ||
                   stlink->chip_id == 0x414 || stlink->chip_id == 0x418 ||
                   stlink->chip_id == 0x420 || stlink->chip_id == 0x428)
                      ? 0x1FFFF7E8u  // STM32F1 UID base
                      : 0x1FFFF7ACu; // STM32F0 / STM32F3 UID base
        break;

    case STM32_FLASH_TYPE_F7:
        uidBase = 0x1FF0F420u; // STM32F7x UID base
        break;

    case STM32_FLASH_TYPE_H7:
        uidBase = 0x1FF1E800u; // STM32H7x UID base
        break;

    case STM32_FLASH_TYPE_L5_U5_H5:
        // This vendored libstlink combines L5/U5/H5 into one flash_type, but H5 uses a
        // different UID base than L5/U5, so disambiguate by chip_id (H503=0x474, H563/H573=0x484).
        uidBase = (stlink->chip_id == 0x474 || stlink->chip_id == 0x484)
                      ? 0x08FFF800u  // STM32H5 (H503/H563/H573) System UID base
                      : 0x0BFA0700u; // STM32L5 / STM32U5x Secure UID base
        break;

    default:
        // Also covers STM32C562RE (chip_id 0x44E): this old libstlink build does not
        // know its flash_type, so it always falls through here. Requires rebuilding
        // libstlink from an upstream version with STM32C5 support to fix properly
        // (see stlink-org/stlink PR #1505).
        emit deviceUIDError("Unsupported MCU family for UID read");
        return;
    }

    int res = stlink_read_mem32(stlink, uidBase, 12);
    if (res != 0)
    {
        emit deviceUIDError(QString("UID read failed at 0x%1").arg(uidBase, 0, 16));
        return;
    }

    QByteArray uid(reinterpret_cast<const char *>(stlink->q_buf), 12);
    bool allZero = std::all_of(uid.begin(), uid.end(), [](char c)
                               { return static_cast<unsigned char>(c) == 0x00; });
    bool allFF = std::all_of(uid.begin(), uid.end(), [](char c)
                             { return static_cast<unsigned char>(c) == 0xFF; });
    if (allZero || allFF)
    {
        emit deviceUIDError("UID appears invalid (all 00/FF)");
        return;
    }

    QString uidHex = uid.toHex().toUpper();
    QString mcu = getDetectedMcu(stlink);
    emit logMessage(QString("MCU: %1").arg(mcu));
    emit deviceUIDAvailable(uidHex, mcu);
}
