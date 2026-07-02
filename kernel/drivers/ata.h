#ifndef ATA_H
#define ATA_H

int ata_init(void);
int ata_drive_exists(int channel, int drive);
const char *ata_get_model(int channel, int drive);
int ata_read_sectors(int channel, int drive, unsigned int lba, unsigned char count, void *buffer);
int ata_write_sectors(int channel, int drive, unsigned int lba, unsigned char count, const void *buffer);

#endif
