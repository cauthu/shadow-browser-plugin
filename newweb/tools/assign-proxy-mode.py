

# takes the list of webclient names from
# "webclient_names.json". randomly split the clients into two groups:
# those using proxy mode "tor" and the rest using proxy mode
# "tproxy-via-tor"
#
# the sizes of the groups are controlled by cmd line args
#

import json
import sys
import random

assert len(sys.argv) == 2

fraction_tproxy_via_tor = float(sys.argv[1])

assert fraction_tproxy_via_tor >= 0
assert fraction_tproxy_via_tor <= 1



SEED = 27281397


random.seed(SEED)


with open('webclient_names.json') as fp:
    all_name_list = json.load(fp)
    assert type(all_name_list) is list
    pass

assert len(set(all_name_list)) == len(all_name_list), 'duplicate names in json'


# assume client names are "webclient<NUM>". we sort by numerical value
# of <NUM>

all_name_list.sort(key=lambda name: int(name[len('webclient'):]))

num_all_names = len(all_name_list)

print 'total number of clients: {}'.format(num_all_names)

num_tproxy_via_tor = int(float(fraction_tproxy_via_tor) * num_all_names)

assert num_tproxy_via_tor >= 0
assert num_tproxy_via_tor <= num_all_names

num_tor = num_all_names - num_tproxy_via_tor

print 'number of "tproxy-via-tor" clients: {}'.format(num_tproxy_via_tor)
print 'number of "tor" clients: {}'.format(num_tor)

assert (num_tor +num_tproxy_via_tor) == num_all_names


## randomly sample the tproxy-via-tor clients

tproxy_via_tor_clients = set(random.sample(all_name_list, num_tproxy_via_tor))
assert len(tproxy_via_tor_clients) == num_tproxy_via_tor

with open('browser_proxy_mode_spec.txt', 'w') as fp:

    print >> fp, '# generated with cmd: {}'.format(' '.join(sys.argv))

    for name in all_name_list:
        if name in tproxy_via_tor_clients:
            proxy_mode = 'tproxy-via-tor'
            pass
        else:
            proxy_mode = 'tor'
            pass
        print >>fp, '{} = {}'.format(name, proxy_mode)
        pass
    pass
