#!/bin/bash

curl -v -X PUT -d "{\"test\":\"test\"}" -H "Content-type: text/json" http://localhost/mongo/
