#ifndef DRIVE_H

// TABLE 1-1 in SMD/CDC docs:
// Data Capacity               40 MB             80 MB
//   Bytes/Track                20 160            20 160
//   Bytes/Cylinder            100 800           100 800
//   Bytes/Spindle          41 428 800        82 958 400
//   Cylinders/Spindle             411               823
#define DRIVE_BYTES_PER_TRACK     (20160)
#define DRIVE_RPM                 (3600)
#define DRIVE_RPS                 (DRIVE_RPM/60)

#if 1
#define DRIVE_CYLINDER_COUNT      (823) // CDC 9762
#else
#define DRIVE_CYLINDER_COUNT      (411) // CDC 9760
#endif
#define DRIVE_HEAD_COUNT          (5)

// NOTE: sector count depends on dip switches on the drive itself; these define
// how many clock cycles a sector consists of, and therefore how often the
// SECTOR impulse is sent.
#define DRIVE_SECTOR_COUNT        (32)

#define DRIVE_H
#endif
