#!/usr/bin/python3

# HB/AG-001,Switzerland,Aargau,Stereberg,872,2861,8.1591,47.2382,8.15910,47.23820,1,0,01/08/2005,31/12/2099,148,22/09/2024,HB3XVA/P 

import re
import csv
from datetime import datetime

h="""typedef struct {
    char ref[11];
    float lat;
    float lon;
} summit;

summit summits[] = {
"""

print(h)

with open('summitslist.csv') as csvfile:
    rdr=csv.reader(csvfile)
    for l in rdr:
        hb=re.search("^HB",l[0])
        if hb is None:
            continue
        vd=datetime.strptime(l[13],"%d/%m/%Y")
        if vd>datetime.now():
            print("{\"" + l[0] + "\"," + str(l[7]) + "," + str(l[6]) + "},")

print("};")

