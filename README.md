# ngx-mongodb

| Authors | Alex Chamberlain <[alex@alexchamberlain.co.uk][]\> |
| ------: | ---------------------------------------------------|

## About

**ngx-mongodb** is an [Nginx][] module to serve content directly from
\`MongoDB <[http://www.mongodb.org/][]\>\` using a REST over HTTP
interface.

## Installation

Installing Nginx modules requires rebuilding Nginx from source:

-   Grab the [Nginx source][Nginx] and unpack it.
-   Clone this repository somewhere on your machine.
-   Change to the directory containing the Nginx source.
-   Now build:

        $ ./configure --add-module=/path/to/ngx-mongodb/source/
        $ make
        $ make install

## Configuration

### Directives

**mongodb-rest**

| syntax  | ```mongodb-rest DB\_NAME [field=QUERY\_FIELD] [type=QUERY\_TYPE] [user=USERNAME] [pass=PASSWORD]``` |
| -----:  | -----    |
| default | *NONE*   |
| context | location |

This directive enables the **ngx-mongodb** module at a given location.
The only required parameter is DB\_NAME to specify the database to serve
files from.

-   *field=* specify the field to query. Supported fields include
    *\_id*. default: *\_id*
-   *type=* specify the type to query. Supported types include
    *objectid*, *string* and *int*. default: *objectid*
-   *user=* specify a username if your mongo database requires
    authentication. default: *NULL*
-   *pass=* specify a password if your mongo database requires
    authentication. default: *NULL*

**mongo**

When connecting to a single server:

| syntax  | ```mongo MONGOD\_HOST``` |
| -----:  | -----    |
| default | ```127.0.0.1:27017``` |
| context | location |

When connecting to a replica set:

| syntax  | ```mongo REPLICA\_SET\_NAME* *MONGOD\_SEED\_1* *MONGOD\_SEED\_2``` |
| -----:  | -----    |
| default | ```127.0.0.1:27017``` |
| context | location |

This directive specifies a mongod or replica set to connect to.
MONGOD\_HOST should be in the form of hostname:port. REPLICA\_SET\_NAME
should be the name of the replica set to connect to.

If this directive is not provided, the module will attempt to connect to
a MongoDB server at *127.0.0.1:27017*.

### Sample Configurations

Here is a sample configuration in the relevant section of an
*nginx.conf*:

    location /db/ {
        mongodb-rest test;
    }

## Credits (nginx-gridfs)

ngx-mongodb is based upon nginx-gridfs.


| Authors of nginx-gridfs | Mike Dirolf <[mike@dirolf.com][]\>, Chris Triolo, and everyone listed below |
| ----------------------: | ------------------------------------------------------------------------------ |

-   Sho Fukamachi (sho) - towards compatibility with newer boost
    versions
-   Olivier Bregeras (stunti) - better handling of binary content
-   Chris Heald (cheald) - better handling of binary content
-   Paul Dlug (pdlug) - mongo authentication
-   Todd Zusman (toddzinc) - gzip handling
-   Kyle Banker (banker) - replica set support

## License

**ngx-mongodb** is dual licensed under the Apache License, Version 2.0
and the GNU General Public License, either version 2 or (at your option)
any later version. See *LICENSE* for details.

  [alex@alexchamberlain.co.uk]: mailto:alex@alexchamberlain.co.uk
  [mike@dirolf.com]: mailto:mike@dirolf.com
  [Nginx]: http://nginx.net/
  [http://www.mongodb.org/]: http://www.mongodb.org/