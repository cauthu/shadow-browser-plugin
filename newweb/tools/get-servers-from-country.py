

# read the countrycode_to_webserver_names json file, and output to
# stdout the list of webserver names from the country specified in
# command line
#
# for use to get list of servers to give to
# "chrome/38.0.2125.122/model_extractor/main.py change-hostnames"
# command, e.g.:
#
# python get-servers-from-country.py US | xargs python3 ~/chrome/38.0.2125.122/model_extractor/main.py change-hostnames ~/nytimes_page_model.json new.json
#


import sys
import json



assert len(sys.argv) == 2

countrycode = sys.argv[1].upper()

# print 'country code: {}'.format(countrycode)

with open('countrycode_to_webserver_names.json') as fp:
    countrycode_to_webserver_names = json.load(fp)
    pass

if countrycode not in countrycode_to_webserver_names:
    raise Exception( 'country code is not found in the json file')
    sys.exit(1)
    pass

server_names = countrycode_to_webserver_names[countrycode]

print ' '.join(server_names)
