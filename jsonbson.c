#include "jsonbson.h"

int json_length(bson* b) {
  bson_iterator i;
  bson_type t;
  const char * key;
  int l = 2; // "{}"

  bson_iterator_init(&i, b);

  while(t = bson_iterator_next(&i)) {
    key = bson_iterator_key(&i);
    
    l += strlen(key) + 3; // "$key":
    switch(t) {
      case BSON_INT:
        {
          int v = bson_iterator_int(&i);
	  do {
            ++l;
	  } while(v /= 10);
	}
	break;
      case BSON_STRING:
        l += bson_iterator_string_len(&i) -1 + 2; // ""
	break;
      default:
        printf("Unsupported type.\n");
	break;
    }

    ++l; // Comma or NULL
  }

  return l;
}

void tojson(bson* b, char * s) {
  bson_iterator i;
  bson_type t;
  const char * key;
  char * pos;

  bson_iterator_init(&i, b);

  s[0] = '{';
  pos = s + 1;

  t = bson_iterator_next(&i);
  if(t) {
    while(1) {
      key = bson_iterator_key(&i);
      
      *pos = '"';
      ++pos;

      memcpy(pos, key, strlen(key));
      pos += strlen(key);

      *pos = '"';
      ++pos;

      *pos = ':';
      ++pos;
      
      switch(t) {
        case BSON_INT:
	  pos += sprintf(pos, "%d", bson_iterator_int(&i));
	  break;
	case BSON_STRING:
	  //l += bson_iterator_string_len(&i) + 2; // ""
	  *pos = '"';
	  ++pos;

	  memcpy(pos, bson_iterator_string(&i), bson_iterator_string_len(&i)-1);
	  pos += bson_iterator_string_len(&i)-1;

	  *pos = '"';
	  ++pos;

	  break;
	default:
	  printf("Unsupported type.\n");
	  break;
      }

      if(t = bson_iterator_next(&i)) {
	*pos = ',';
	++pos;
      } else {
        break;
      }
    }
  }

  *pos = '}';
  ++pos;

  *pos = '\0';
}

