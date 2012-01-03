#ifndef JSONBSON_H
#define JSONBSON_H

#include <mongodb-c/bson.h>

int json_length(const bson* b);
void tojson(const bson* b, char * s);

#endif // JSONBSON_H
