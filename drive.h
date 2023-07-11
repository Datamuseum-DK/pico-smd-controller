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

#define DRIVE_CYLINDER_COUNT      (1024)
#define DRIVE_HEAD_COUNT          (5)

#define DRIVE_H
#endif
