#include "stlinkwriter.h"
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include "stlink.h"
extern "C"
{
#include "common_flash.h"
#include "read_write.h"
}

StLinkWriter::StLinkWriter(QObject *parent) : QObject(parent)
{
}

void StLinkWriter::flash(stlink_t* stlink, const QString &filePath)
{
    if (!stlink)
    {
        emit operationFinished(false, "Device not connected");
        return;
    }

    emit operationStarted("Flashing");
    emit logMessage("Flashing file: " + QFileInfo(filePath).fileName());

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit operationFinished(false, "Could not open firmware file");
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty())
    {
        emit operationFinished(false, "Firmware file is empty");
        return;
    }

    // Ensure we are in the right mode
    stlink_reset(stlink, RESET_HARD);
    stlink_force_debug(stlink);
    stlink_status(stlink);

    // Unlock flash if necessary
    unlock_flash_if(stlink);

    stm32_addr_t flash_base = stlink->flash_base;
    uint32_t size = data.size();

    emit logMessage("Erasing flash...");
    emit progressChanged(0, 100);

    // Use page erase instead of mass erase to avoid timeouts and provide progress
    uint32_t page_size = stlink->flash_pgsz;
    if (page_size == 0)
        page_size = 2048; // Safety fallback

    uint32_t addr = flash_base;
    uint32_t end_addr = flash_base + size;

    int total_pages = (size + page_size - 1) / page_size;
    int erased_pages = 0;

    while (addr < end_addr)
    {
        int res = stlink_erase_flash_page(stlink, addr);
        if (res != 0)
        {
            emit operationFinished(false, QString("Failed to erase page at 0x%1").arg(addr, 0, 16));
            return;
        }
        addr += page_size;
        erased_pages++;

        // Progress 0-50%
        int percent = (int)((double)erased_pages / total_pages * 50.0);
        emit progressChanged(percent, 100);

        // Keep GUI responsive
        QThread::msleep(10);
    }

    emit logMessage("Flash erased. Writing firmware...");
    emit progressChanged(50, 100);

    // Ensure flash is unlocked before writing
    unlock_flash_if(stlink);

    emit progressChanged(75, 100);

    // Let libstlink handle the MCU-specific programming granularity internally.
    // Repeated short stlink_write_flash() calls can fail on some F4 targets
    // after the first successful block.
    QByteArray writeBuffer = data;
    const int res = stlink_write_flash(stlink,
                                       flash_base,
                                       reinterpret_cast<uint8_t *>(writeBuffer.data()),
                                       size,
                                       0,
                                       NO_ERASE);

    if (res != 0)
    {
        emit operationFinished(false, QString("Failed to write firmware. Error: %1").arg(res));
        return;
    }

    emit progressChanged(100, 100);
    emit logMessage("Flashing complete. Resetting device...");

    stlink_reset(stlink, RESET_HARD);
    stlink_run(stlink, RUN_NORMAL);

    emit operationFinished(true, "Firmware flashed successfully");
}
