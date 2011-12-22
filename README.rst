ngx-mongodb
============
Forked from mdirolf/nginx-gridfs

:Authors:
    Alex Chamberlain <alex@alexchamberlain.co.uk>

:Authors of nginx-gridfs:
    Mike Dirolf <mike@dirolf.com>, Chris Triolo, and everyone listed in the Credits (nginx-gridfs) section below

About
=====
**ngx-mongodb** is an `Nginx <http://nginx.net/>`_ module to serve
content directly from `MongoDB <http://www.mongodb.org/>` using
a REST over HTTP interface.

.. Version
  ============
  The minor version will be incremented with each release until
  a stable 1.0 is reached. To check out a particular version::

  $ git checkout v0.8

Installation
============
Installing Nginx modules requires rebuilding Nginx from source:

* Grab the `Nginx source <http://nginx.net/>`_ and unpack it.
* Clone this repository somewhere on your machine.
* Change to the directory containing the Nginx source.
* Now build::

    $ ./configure --add-module=/path/to/ngx-mongodb/source/
    $ make
    $ make install

Configuration
=============

Directives
----------

**mongodb-rest**

:syntax: *mongodb-rest DB_NAME [field=QUERY_FIELD] [type=QUERY_TYPE] [user=USERNAME] [pass=PASSWORD]* 
:default: *NONE*
:context: location

This directive enables the **ngx-mongodb** module at a given location. The 
only required parameter is DB_NAME to specify the database to serve files from. 

.. * *root_collection=* specify the root_collection(prefix) of the GridFS. default: *fs*
* *field=* specify the field to query. Supported fields include *_id*. default: *_id*
* *type=* specify the type to query. Supported types include *objectid*, *string* and *int*. default: *objectid*
* *user=* specify a username if your mongo database requires authentication. default: *NULL*
* *pass=* specify a password if your mongo database requires authentication. default: *NULL*

**mongo**

When connecting to a single server::

:syntax: *mongo MONGOD_HOST*
:default: *127.0.0.1:27017*
:context: location

When connecting to a replica set::

:syntax: *mongo REPLICA_SET_NAME* *MONGOD_SEED_1* *MONGOD_SEED_2*
:default: *127.0.0.1:27017*
:context: location

This directive specifies a mongod or replica set to connect to. MONGOD_HOST should be in the
form of hostname:port. REPLICA_SET_NAME should be the name of the replica set to connect to.

If this directive is not provided, the module will attempt to connect to a MongoDB server at *127.0.0.1:27017*.

Sample Configurations
---------------------

Here is a sample configuration in the relevant section of an *nginx.conf*::

  location /db/ {
      mongodb-rest test;
  }

.. Known Issues / TODO / Things You Should Hack On
  ===============================================

  * HTTP range support for partial downloads
  * Better error handling / logging

Credits (nginx-gridfs)
=====================

* Sho Fukamachi (sho) - towards compatibility with newer boost versions
* Olivier Bregeras (stunti) - better handling of binary content
* Chris Heald (cheald) - better handling of binary content
* Paul Dlug (pdlug) - mongo authentication
* Todd Zusman (toddzinc) - gzip handling
* Kyle Banker (banker) - replica set support

License
=======
**ngx-mongodb** is dual licensed under the Apache License, Version
2.0 and the GNU General Public License, either version 2 or (at your
option) any later version. See *LICENSE* for details.
