#ifndef CA_BUNDLE_H
#define CA_BUNDLE_H
#include "../include/types.h"

/* Mozilla CA bundle — embedded for HTTPS certificate verification */
#define CA_BUNDLE_LEN 189463
extern const unsigned char ca_bundle_pem[CA_BUNDLE_LEN];

#endif /* CA_BUNDLE_H */
