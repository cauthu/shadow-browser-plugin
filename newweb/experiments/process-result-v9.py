#!/usr/bin/env python3

import pdb
import re
import sys
import shutil
import os
import math
import argparse
import pickle
import bz2
import logging
from collections import namedtuple
import datetime
from common import convert_to_second, genCDF
import math


g_version = 9

    
# represents results of one simulation file, containing many page load
# results
# 
class OneSimulationResult:
    def __init__(self, description=''):
        # pickling can be efficient by sharing pointers, but each
        # parsed url, even though identical lexicographically, is a
        # different string object and won't be able to take advantage
        # of the efficient pickling. so we use an identity map, from
        # url to itself, so that each load's url is a reference to the
        # value in the map
        ## self.urlMap = {}
        ## # similar motivations as url above
        ## self.hostMap = {}
        ## self.modeMap = {}
        # "loadResults" contain all types of client download results,
        # web, bulk, perf, vanilla, pnp, etc.
        #
        # completionTime is when the experiment finished.
        #
        self.shadowRelease = None
        self.torVersion = None
        self.completionTime = None
        self.description = description
        self.loadResults = []
        pass
    pass

class OneFetchResult:
    def __init__(self, *, timestamp, host, loadnum, proxymode, succeeded, url,
                 starttime=None, plt=None, ttfb=None, numreqs=None,
                 reason=None, numsuccess=None, numfailed=None, numforced=None,
                 numafterdomloadevent=None):
        self.starttime = starttime
        self.timestamp = timestamp
        self.host = host
        self.loadnum = loadnum
        self.proxymode = proxymode
        self.succeeded = succeeded
        self.url = url

        self.plt = plt
        self.ttfb = ttfb
        self.numReqs = numreqs
        self.numSuccess = numsuccess
        self.numFailed = numfailed
        self.numAfterDOMLoadEvent = numafterdomloadevent
        self.numForced = numforced

        # in case of failure
        self.reason = reason

        pass
    pass

#############################################

commonPattern = r'^[0-9:\.]+ \[thread-[0-9]+\] (?P<time>([0-9:]+|n/a)) \[.+\] \[((?P<host>.+)-[\.\d]+|n/a)\] '

# # 4:44:15:574535 [thread-16] 0:59:59:497722030 [browser-message] [webclient503.1-4.10.0.0] [report_result] loadnum= 29, pnp: success: start= ... plt= ... url= [...] ttfb= 2909 rxbodybytes= ... txbytes= ... rxbytes= ... numobjects= 34 numerrorobjects= 0

# # don't care about the ip address
# browserResultLinePattern = re.compile(
#     commonPattern + \
#     '\[report_result\] loadnum= (?P<loadnum>\d+), (?P<mode>(vanilla|pnp)): ' \
#     '(?P<status>.+): start= (?P<starttime>\d+) plt= (?P<plt>[-+]?\d+) ' \
#     'url= \[(?P<url>http://.+)\] ttfb= (?P<ttfb>[-+]?\d+) ' \
#     'rxbodybytes= (?P<size>[-+]?\d+) txbytes= (?P<sentbytes>[-+]?\d+) '\
#     'rxbytes= (?P<recvbytes>[-+]?\d+) numobjects= (?P<numobjects>\d+) ' \
#     'numerrorobjects= (?P<numerrorobjects>\d+)')

# # 2:35:21:283294 [thread-8] 0:59:59:306193864 [filetransfer-message] [bulkclient30-208.5.0.0] [_filetransfer_logCallback] [fg-download-complete] got first bytes in 3.870 seconds and 5242880 of 5242880 bytes in 109.102 seconds (download 48)

# filetransferResultLinePattern = re.compile(
#     commonPattern + \
#     '\[_filetransfer_logCallback\] \[fg-download-complete\] got first bytes ' \
#     'in (?P<ttfb>[-+]?[\.\d]+) seconds and (?P<recvbytes>[-+]?\d+) of (?P<size>[-+]?\d+) bytes ' \
#     'in (?P<plt>[-+]?[\.\d]+) seconds \(download (?P<loadnum>\d+)\)')

# # 4:43:58:228083 [thread-1] 0:59:56:800357114 [browser-message] [webclient580.1-106.9.0.0] [report_failed_load] loadnum= 26, pnp: FAILED: start= ... reason= [timedout] url= [...] rxbytes= ...
# failedResultPattern = re.compile(
#     commonPattern + \
#     '\[report_failed_load\] loadnum= (?P<loadnum>\d+), (?P<mode>(vanilla|pnp)): ' \
#     'FAILED: start= (?P<starttime>\d+) reason= \[(?P<reason>.*)\] url= \[(?P<url>http://.+)\] rxbytes= (?P<recvbytes>[-+]?\d+)')

sharedstrmap = {}

def get_shared_ref(key, m):
    if key in m:
        retkey = m[key]
        pass
    else:
        m[key] = key
        retkey = key
        pass
    return retkey

    # retkey = m.get(key, None)
    # if retkey is None:
    #     m[key] = key
    #     retkey = key
    #     pass
    # return retkey


# def getResultsFromFile(onesimulationresult, fileobj):
#     prevLine = line = None
#     torVersion = None
#     line = fileobj.readline().strip()
#     match = re.match(shadowStartupPattern, line)
#     assert match
#     shadowRelease = match.group('shadowRelease')
#     expStartTime = match.group('expStartTime')

#     while True:
#         prevLine = line # save line
#         line = fileobj.readline()
#         if line == '':
#             # readline() returns empty string on EOF
#             break
#         line = line.strip()
#         logging.debug('line: [%s]' % (line))

#         is_filetransfer = False

#         oneresultobj = None

#         if not match:
#             match = re.match(browserResultLinePattern, line)
#             if match:
#                 succeeded = match.group('status') == 'success'
#                 plt = int(match.group('plt'))
#                 ttfb = int(match.group('ttfb'))
#                 size = int(match.group('size'))
#                 sentbytes = int(match.group('sentbytes'))
#                 recvbytes = int(match.group('recvbytes'))
#                 numobjects = int(match.group('numobjects'))
#                 numerrorobjects = int(match.group('numerrorobjects'))
#                 loadnum, mode, url, starttime = match.group(
#                     'loadnum', 'mode', 'url', 'starttime')
#                 starttime = int(starttime)
#                 assert(starttime > 0)
#                 assert(mode in ('vanilla', 'pnp'))
#                 loadnum = int(loadnum)
#                 assert(loadnum > 0)
#                 if succeeded:
#                     assert(plt > 0)
#                     assert(ttfb > 0)
#                     assert(recvbytes > 0)
#                     assert(sentbytes > 0)
#                     assert(numerrorobjects == 0)
#                     assert(numobjects > 0)
#                 pass
#                 assert(size >= 0)
#                 assert(recvbytes >= 0)
#                 assert(sentbytes >= 0)
#                 assert(numobjects >= 0)

#                 oneresultobj = OneFetchResult(
#                     loadnum=loadnum,
#                     url=get_shared_ref(url, sharedstrmap),
#                     mode=get_shared_ref(mode, sharedstrmap),
#                     succeeded=True, plt=plt,
#                     ttfb=ttfb, numobjects=numobjects, size=size,
#                     recvbytes=recvbytes, starttime=starttime,
#                     )

#                 onesimulationresult.loadResults.append(oneresultobj)
#                 pass
#             pass

#         ########
#         if not match:
#             match = re.match(failedResultPattern, line)
#             if match:
#                 loadnum, mode, url, reason, starttime = match.group(
#                     'loadnum', 'mode', 'url', 'reason', 'starttime')
#                 recvbytes = int(match.group('recvbytes'))
#                 starttime = int(starttime)
#                 assert(starttime > 0)
#                 loadnum = int(loadnum)
#                 assert(loadnum > 0)
#                 assert(mode in ('vanilla', 'pnp'))

#                 oneresultobj = OneFetchResult(
#                     loadnum=loadnum,
#                     url=get_shared_ref(url, sharedstrmap),
#                     mode=get_shared_ref(mode, sharedstrmap),
#                     succeeded=False,
#                     starttime=starttime,
#                     reason=get_shared_ref(reason, sharedstrmap),
#                     recvbytes=recvbytes,
#                     )
#                 onesimulationresult.loadResults.append(oneresultobj)
#                 pass
#             pass

#         if not match:
#             match = re.match(filetransferResultLinePattern, line)
#             if match:
#                 # filetransfer plugin outputs in seconds, so we
#                 # convert to milliseconds
#                 is_filetransfer = True
#                 plt = int(float(match.group('plt')) * 1000)
#                 ttfb = int(float(match.group('ttfb')) * 1000)
#                 size = int(match.group('size'))
#                 recvbytes = int(match.group('recvbytes'))
#                 succeeded = (recvbytes == size)
#                 host, loadnum = match.group('host', 'loadnum')
#                 # file transfer always uses mode vanilla
#                 mode = 'vanilla'

#                 oneresultobj = OneFetchResult(
#                     loadnum=loadnum,
#                     url=None, mode=mode, succeeded=succeeded,
#                     plt=plt, ttfb=ttfb, size=size,
#                     )
#                 onesimulationresult.loadResults.append(oneresultobj)
#                 pass
#             pass

#         #######
#         if match:
#             # get the common fields

#             # timestamp has format: <hr>:<min>:<sec>:<microsec>. we
#             # keep only millisecond granularity
#             t = map(int, match.group('time').split(':'))
#             assert (4 == len(t))
#             timestamp = (t[0]*3600*1000)  +  (t[1]*60*1000) \
#                         + (t[2]*1000)  +  (int(float(t[3]) / 1000))
#             assert(timestamp != None) and (timestamp > 0)
#             host = get_shared_ref(match.group('host'), sharedstrmap)
#             assert(host != None)

#             oneresultobj.timestamp = timestamp
#             oneresultobj.host = host


#             if is_filetransfer and oneresultobj.succeeded:
#                 oneresultobj.starttime = oneresultobj.timestamp - oneresultobj.plt
#                 pass

#             pass # end if match

#         pass

#     assert prevLine != None
#     match = re.match(cleanShutdownPattern, prevLine)
#     if not match:
#         print()
#         print 'ERROR: result in "%s" did not shut down cleanly. exiting now...' % (fileobj.name)
#         print
#         sys.exit(1)
#         pass

#     if match.group('shadowRelease') != shadowRelease:
#         print
#         print 'ERROR: shadow release at shutdown %s != relase at start %s' % (
#             match.group('shadowRelease'), shadowRelease)
#         print
#         sys.exit(1)
#         pass

#     completionTime = match.group('expDoneTime')
#     onesimulationresult.shadowRelease = shadowRelease
#     onesimulationresult.startTime = expStartTime
#     onesimulationresult.completionTime = completionTime
#     onesimulationresult.torVersion = torVersion

#     return

def really_parse_shadow_log_file(onesimulationresult, fileobj):

    commonPattern = r'^[0-9:\.]+ \[thread-[0-9]+\] n/a \[shadow-message\] \[n/a\] \[.+\] '
    # 00:00:00.021258 [thread-0] n/a [shadow-message] [n/a] [shd-master.c:143] [master_run] Shadow v1.11.2-3-g194f329 2016-10-04 (built 2016-11-11)
    shadowVersionPattern = re.compile(
        commonPattern + \
        '\[master_run\] Shadow (?P<shadowRelease>.+)')

    # 00:00:00.021370 [thread-0] n/a [shadow-message] [n/a] [shd-master.c:153] [master_run] Shadow initialized at 2016-11-19 00:14:35 using GLib v2.48.1 and IGraph v0.7.1
    shadowStartupPattern = re.compile(
        commonPattern + \
        '\[master_run\] Shadow initialized at (?P<expStartTime>.+) using.*')

    line = fileobj.readline().strip()
    match = re.match(shadowVersionPattern, line)
    assert match
    shadowRelease = match.group('shadowRelease')

    line = fileobj.readline().strip()

    line = fileobj.readline().strip()
    match = re.match(shadowStartupPattern, line)
    assert match
    expStartTime = match.group('expStartTime')
    
    prevLine = line = None
    while True:
        prevLine = line # save line
        line = fileobj.readline()
        if line == '':
            # readline() returns empty string on EOF
            break
        pass

    # 00:00:00.000000 [thread-0] n/a [shadow-message] [n/a] [shd-master.c:88] [master_free] Shadow v1.11.2-3-g194f329 2016-10-04 (built 2016-11-11) shut down cleanly at 2016-11-19 03:31:09
    cleanShutdownPattern = re.compile(
        r'^[0-9\.:]+ \[thread-[0-9]+\] .*' + \
        '\[(engine|master)_free\] Shadow (?P<shadowRelease>.+) shut down cleanly at (?P<expDoneTime>.+)$')

    match = re.match(cleanShutdownPattern, prevLine)
    if not match:
        print()
        print('ERROR: result in "%s" did not shut down cleanly. exiting now...' % (fileobj.name))
        print()
        sys.exit(1)
        pass
    completionTime = match.group('expDoneTime')

    onesimulationresult.shadowRelease = shadowRelease
    onesimulationresult.startTime = expStartTime
    onesimulationresult.completionTime = completionTime

    pass

def parse_shadow_log_file(onesimulationresult, expdir):
    for ext, openfunc in (('', open),
                          ('.bz2', bz2.BZ2File)):
        fpath = os.path.join(expdir, 'shadow.log{}'.format(ext))
        try:
            with openfunc(fpath) as fp:
                return really_parse_shadow_log_file(onesimulationresult, fp)
            pass
        except FileNotFoundError as exc:
            logging.debug('"{fpath}" not found'.format(fpath=fpath))
            pass
        pass

    raise FileNotFoundError(
        'no shadow log file is found in experiment dir "{}"'.format(expdir))

# "00:21:21.466 INFO  - file.cpp :398, func() ::   "
newweb_common_log_prefix = \
  r'^(?P<ts>[0-9:\.]+) [A-Z0-9]+[ ]+- .+ :\d+, .+\(\) ::   '

# 00:21:21.466 INFO  - driver_renderer.cpp :398, _renderer_load_page() ::   driver= 1: start loading page [www.wikipedia.org]
webclient_driver_start_loading_pattern = re.compile(
    newweb_common_log_prefix + \
    'driver= \d+: start loading page \[(?P<url>.+)\]$')
    
# 00:21:24.299 INFO  - driver_renderer.cpp :224, _renderer_handle_PageLoaded() ::   driver= 1: DOM "load" event has fired; start waiting for more requests
webclient_driver_dom_load_event_fired_pattern = re.compile(
    newweb_common_log_prefix + \
    'driver= \d+: DOM "load" event has fired; start waiting for more requests$')

# 00:21:27.690 INFO  - driver_renderer.cpp :192, _report_result() ::   loadnum= 3, webmode= vanilla, proxyMode= tor: loadResult= OK: startSec= 1281 plt= 2833 page= [www.wikipedia.org] ttfb= 824 numReqs= 7 numSuccess= 7 numFailed= 0 numAfterDOMLoadEvent= 2 numForced= 2
webclient_driver_report_result_pattern = re.compile(
    newweb_common_log_prefix + \
    'loadnum= (?P<loadnum>\d+), webmode= (?P<webmode>.+), proxyMode= (?P<proxymode>.+): '
    'loadResult= (?P<loadresult>.+): startSec= (?P<startsec>\d+) plt= (?P<plt>\d+) '
    'page= \[(?P<url>.+)\] ttfb= (?P<ttfb>\d+) numReqs= (?P<numreqs>\d+) '
    'numSuccess= (?P<numsuccess>\d+) numFailed= (?P<numfailed>\d+) '
    'numAfterDOMLoadEvent= (?P<numafterdomload>\d+) numForced= (?P<numforced>\d+)')

g_known_loadresults = set(['OK', 'FAILED', 'TIMEDOUT'])
g_known_proxymodes = set(['tor', 'tproxy-via-tor'])

def parse_webclient_driver_log(onesimulationresult, hostname, fp):

    # 00:21:21.466 INFO  - driver_renderer.cpp :398, _renderer_load_page() ::   driver= 1: start loading page [www.wikipedia.org]
    # 00:21:24.299 INFO  - driver_renderer.cpp :224, _renderer_handle_PageLoaded() ::   driver= 1: DOM "load" event has fired; start waiting for more requests
    # 00:21:27.690 INFO  - driver.cpp :182, _on_wait_for_more_requests_timer_fired() ::   driver= 1: done waiting for more requests
    # 00:21:27.690 INFO  - driver_renderer.cpp :192, _report_result() ::   loadnum= 3, webmode= vanilla, proxyMode= tor: loadResult= OK: startSec= 1281 plt= 2833 page= [www.wikipedia.org] ttfb= 824 numReqs= 7 numSuccess= 7 numFailed= 0 numAfterDOMLoadEvent= 2 numForced= 2

    # for verification purpose: should match stuff reported
    # subsequently
    class CurrentLoadInfo(object):
        def __init__(self):
            self.reset()
            pass

        def reset(self):
            self.url = None
            self.start_ts = None
            self.dom_load_event_ts = None
            pass

        pass

    loadnum = 0
    current_load_info = CurrentLoadInfo()

    while True:
        line = fp.readline()
        if line == '':
            # readline() returns empty string on EOF
            break
        line = line.strip()
        logging.debug('line: [%s]' % (line))

        match = None

        #####
        match = re.match(webclient_driver_start_loading_pattern, line)
        if match:
            loadnum += 1
            current_load_info.url = match.group('url')
            ts = match.group('ts')
            current_load_info.start_ts = convert_to_second(ts)
            continue

        #####
        match = re.match(webclient_driver_dom_load_event_fired_pattern, line)
        if match:
            assert loadnum > 0
            ts = match.group('ts')
            current_load_info.dom_load_event_ts = convert_to_second(ts)
            continue

        #####
        match = re.match(webclient_driver_report_result_pattern, line)
        if match:
            assert loadnum > 0
            ts = match.group('ts')
            timestamp = convert_to_second(ts)
            reportloadnum, startsec, plt, ttfb, numreqs, numsuccess, numfailed, numafterdomloadevent, \
              numforced = list(map(int, match.group('loadnum', 'startsec', 'plt', 'ttfb', 'numreqs',
                                                    'numsuccess', 'numfailed', 'numafterdomload',
                                                    'numforced')))
            proxymode, loadresult, url = match.group('proxymode', 'loadresult', 'url')
            succeeded = (loadresult == 'OK')

            assert url == current_load_info.url
            assert reportloadnum == loadnum, '{} {}'.format(reportloadnum, loadnum)
            assert proxymode in g_known_proxymodes, proxymode
            assert loadresult in g_known_loadresults
            assert current_load_info.dom_load_event_ts or (not succeeded)

            current_load_info.reset()

            reason=None if succeeded else get_shared_ref(loadresult, sharedstrmap)

            host=get_shared_ref(hostname, sharedstrmap)
            oneresultobj = OneFetchResult(
                timestamp=timestamp,
                host=host,
                loadnum=reportloadnum,
                proxymode=get_shared_ref(proxymode, sharedstrmap),
                succeeded=succeeded,
                url=get_shared_ref(url, sharedstrmap), plt=plt, ttfb=ttfb, reason=reason,
                starttime=startsec, numreqs=numreqs, numsuccess=numsuccess, numfailed=numfailed,
                numafterdomloadevent=numafterdomloadevent, numforced=numforced)

            onesimulationresult.loadResults.append(oneresultobj)
            # assert False
            continue
        
        pass
    
    pass

def parse_webclient(onesimulationresult, outdir):
    logging.debug('parsing webclient in {}'.format(outdir))
    hostname = os.path.basename(outdir)
    for fname in os.listdir(outdir):
        if fname.startswith('stdout-driver-'):
            fpath = os.path.join(outdir, fname)
            with open(fpath) as fp:
                logging.debug('parsing webclient driver log "{}"'.format(fname))
                parse_webclient_driver_log(onesimulationresult, hostname, fp)
                pass
            pass
        pass
    pass

#############################################
def parse(expdir, outputfilepath='parsedPickledResults-v%u.bz2' % (g_version),
          description='', force=False):
    if os.path.exists(outputfilepath) and (not force):
        print('output file path "{}" already exists'.format(outputfilepath))
        sys.exit(0)
        pass

    onesimulationresult = OneSimulationResult(description=description)

    parse_shadow_log_file(onesimulationresult, expdir)

    hosts_data_dir = os.path.join(expdir, 'shadow.data/hosts')

    num_webclients = 0

    for hostname in os.listdir(hosts_data_dir):
        if hostname.startswith('webclient'):
            num_webclients += 1
            parse_webclient(onesimulationresult,
                            os.path.join(hosts_data_dir, hostname))
            pass
        # else:
        #     raise Exception('unhandled hostname "{}"'.format(hostname))
        pass

    logging.info('parsed: num_webclients= {num_webclients}'.format(
        num_webclients=num_webclients))

    pickler = pickle.Pickler(bz2.BZ2File(outputfilepath, 'w'),
                             protocol=2)
    pickler.dump([g_version, onesimulationresult])
    return


def analyzeClientResults(results, output_cdf_prefix,
                         plt_scale=1.0, ttfb_scale=1.0,
                         plt_bucketsize=0, ttfb_bucketsize=0,
                         commentLines=[],
                         ):

    print( "total num result", len(results))

    numbadresult = len(list(filter(lambda r: not r.succeeded, results)))
    numtimedout = len(list(filter(lambda r: (not r.succeeded) and (r.reason == 'TIMEDOUT'), results)))
    numfailed = len(list(filter(lambda r: (not r.succeeded) and (r.reason == 'FAILED'), results)))
    assert numbadresult == (numtimedout + numfailed)
    print( 'numbadresult (succeeded != true): %u. due to...\n' \
          '    timeout: %u\n' \
          '    failed: %u' % (numbadresult,
                                    numtimedout, numfailed))

    # "bufloot" is "buflo over tor"
    tor_plts = []
    bufloot_plts = []
    tor_ttfbs = []
    bufloot_ttfbs = []

    proxymode_to_shorter_name = {
        'tor': 'tor',
        'tproxy-via-tor': 'bufloot',
        }

    proxymode_to_plt_lst = {
        'tor': tor_plts,
        'tproxy-via-tor': bufloot_plts,
        }
    proxymode_to_ttfb_lst = {
        'tor': tor_ttfbs,
        'tproxy-via-tor': bufloot_ttfbs,
        }

    for r in results:
        if r.succeeded:
            proxymode_to_plt_lst[r.proxymode].append(round(float(r.plt) * plt_scale, 1))
            proxymode_to_ttfb_lst[r.proxymode].append(round(float(r.ttfb) * ttfb_scale, 1))
            pass
        pass

    for (proxymode, plt_lst, ttfb_lst) in (
            ('tor', tor_plts, tor_ttfbs),
            ('tproxy-via-tor', bufloot_plts, bufloot_ttfbs),
            ):
        if plt_lst:
            genCDF(sorted(plt_lst),
                   toFilepath='%s-%s-plt.cdf' % (output_cdf_prefix, proxymode_to_shorter_name[proxymode]),
                   bucketsize=plt_bucketsize,
                   commentLines=commentLines,
                   )
            pass
        if ttfb_lst:
            genCDF(sorted(ttfb_lst),
                   toFilepath='%s-%s-ttfb.cdf' % (output_cdf_prefix, proxymode_to_shorter_name[proxymode]),
                   bucketsize=ttfb_bucketsize,
                   commentLines=commentLines,
                   )
            pass
        pass

    return


# #############################################
# def unpickleResults(filepaths):
#     results = []
#     for filepath in filepaths:
#         f = bz2.BZ2File(filepath)
#         unpickler = pickle.Unpickler(f)
#         try:
#             version, onesimulationresult = unpickler.load()
#             if version != g_version:
#                 print( 'ERROR: file [%s] has different version [%s]. exiting...' % (
#                     filepath, str(version)))
#                 sys.exit(1)
#                 pass
#             pass
#         except:
#             print 'ERROR: cannot unpickle file [%s]' % (filepath)
#             sys.exit(1)
#             pass
#         del unpickler
#         results.append(
#             (filepath, onesimulationresult),
#             )
#         f.close()
#         pass
#     return results

# #############################################
# def count_downloads_and_bytes_from_fetch_results(results,
#                                                  startAfter=0,
#                                                  doneBefore=None,
#                                                  ):
#     if doneBefore is None:
#         doneBefore = sys.maxint
#         pass

#     clienttypes = (
#         'web',
#         'bulk',
#         'bridgeweb',
#         'bridgebulk',
#         'perf',
#         )

#     total_download_bytes = {}
#     total_success_downloads = {}
#     total_failed_downloads = {}
#     for clienttype in clienttypes:
#         total_download_bytes[clienttype] = 0L
#         total_success_downloads[clienttype] = 0L
#         total_failed_downloads[clienttype] = 0L
#         pass

#     clienttypepattern = re.compile(r'^(?P<type>.+)client')
#     for result in results:
#         if not (result.starttime >= startAfter and result.timestamp <= doneBefore):
#             continue
#         recvbytes = result.recvbytes
#         match = re.search(clienttypepattern, result.host)
#         assert match
#         clienttype = match.group('type')
#         total_download_bytes[clienttype] += recvbytes
#         if result.succeeded:
#             total_success_downloads[clienttype] += 1
#             pass
#         else:
#             total_failed_downloads[clienttype] += 1
#             pass
#         pass

#     all_client_total_bytes = sum(total_download_bytes.values())

#     print
#     print 'client application downloads:'
#     print '  counts:'
#     for clienttype in clienttypes:
#         print '    %s clients: success: %u, FAILED: %u' % (
#             clienttype, total_success_downloads[clienttype],
#             total_failed_downloads[clienttype],
#             )
#         pass

#     print
#     print '  bytes:', sizeof_fmt(all_client_total_bytes)
#     if all_client_total_bytes > 0:
#         for clienttype in clienttypes:
#             print '    %s clients: %s, fraction: %0.2f %%' % (
#                 clienttype, sizeof_fmt(total_download_bytes[clienttype]),
#                 float(total_download_bytes[clienttype]) / all_client_total_bytes * 100)
#             pass
#         pass

#     pass

# #############################################
# def count_downloads_and_bytes(filepaths,
#                               startAfter=0,
#                               doneBefore=None,
#                               ):
#     results = unpickleResults(filepaths)

#     print
#     print 'ONLY counting downloads that'
#     print '  start at and after %s' % (convert_ms_to_str(startAfter))
#     if doneBefore != None:
#         print '  finish at and before %s' % (convert_ms_to_str(doneBefore))
#         pass
#     print

#     combined_fetch_results = []
#     combined_node_heartbeats = []
#     for (filepath, onesimulationresult) in results:
#         print '=============='
#         print 'for result file:', filepath
#         count_downloads_and_bytes_from_fetch_results(
#             onesimulationresult.loadResults, startAfter=startAfter, doneBefore=doneBefore)
#         combined_fetch_results.extend(onesimulationresult.loadResults)
#         combined_node_heartbeats.extend(onesimulationresult.nodeHeartbeats)
#         print
#         pass

#     if len(filepaths) > 1:
#         print '=============='
#         print 'aggregate:'
#         count_downloads_and_bytes_from_fetch_results(
#             combined_fetch_results, startAfter=startAfter, doneBefore=doneBefore)
#         print
#         pass

#     return

# #############################################
def analyze(filepaths, web_urls=set(), web_plt_bucketsize=None,
            show_info_only=False,
            web_only=False,
            web_ttfb_bucketsize=0,
            bulk_fdt_bucketsize=0,
            bulk_ttfb_bucketsize=0,
            min_num_objects=None,
            output_cdf_prefix='', startAfter=0, doneBefore=None,
            commentLines=[]):

    aggregate = True
    if aggregate:
        results = []
        commentLines.append('==================================================')
        header = '[pickle file]: [shadow release] [tor version] [embedded description] [experiment completion time]'
        commentLines.append(header)
        print( header)
        for filepath in filepaths:
            f = bz2.BZ2File(filepath)
            unpickler = pickle.Unpickler(f)
            try:
                version, onesimulationresult = unpickler.load()
                if version != g_version:
                    print( 'ERROR: file [%s] has different version [%s]. exiting...' % (
                        filepath, str(version)))
                    sys.exit(1)
                    pass
                pass
            except:
                print( 'ERROR: cannot unpickle file [%s]' % (filepath))
                sys.exit(1)
                pass
            del unpickler
            results.extend(onesimulationresult.loadResults)
            commentLine = '[{filepath}]: [{shadowRelease}] [{torVersion}] [{desc}] [{completionTime}]'.format(
                filepath=filepath,
                shadowRelease=onesimulationresult.shadowRelease,
                torVersion=onesimulationresult.torVersion,
                desc=onesimulationresult.description,
                completionTime=onesimulationresult.completionTime)
            commentLines.append(commentLine)
            print(( commentLine))
            f.close()
            pass

        pass

    if show_info_only:
        return

    print( '========== summary ==========')
    print( 'web results')
    for (hostprefix, outputprefix) in (('webclient', 'web'),
                                       # ('bridgewebclient', 'bridgeweb-'),
                                       ):
        analyzeClientResults(
            list(filter(lambda r: r.host.startswith(hostprefix) \
                   and (False if (len(web_urls) > 0 and r.url not in web_urls) else True) \
                   # and r.numobjects >= min_num_objects \
                   and r.starttime >= startAfter \
                   and (True if (doneBefore is None) else (r.timestamp <= doneBefore)),
                   results)),
            plt_bucketsize=web_plt_bucketsize, ttfb_bucketsize=web_ttfb_bucketsize,
            plt_scale=float(1)/1000, ttfb_scale=float(1)/1000,
            output_cdf_prefix=output_cdf_prefix + outputprefix,
            commentLines=commentLines,
            )
        pass

    if web_only:
        return

    # for (hostprefix, outputprefix) in (('bulkclient', 'bulk-'),
    #                                    ('bridgebulkclient', 'bridgebulk-'),
    #                                    ('perfclient50k', 'perf50k-'),
    #                                    ('perfclient1m', 'perf1m-'),
    #                                    ('perfclient5m', 'perf5m-'),
    #                                    ):
    #     print( '---------------------')
    #     print( hostprefix, 'results')
    #     analyzeClientResults(
    #         list(filter(lambda r: r.host.startswith(hostprefix) \
    #                and r.starttime >= startAfter and \
    #                (True if (doneBefore is None) else (r.timestamp <= doneBefore)),
    #                results)),
    #         plt_bucketsize=bulk_fdt_bucketsize, ttfb_bucketsize=bulk_ttfb_bucketsize,
    #         plt_scale=float(1)/(1000), ttfb_scale=float(1)/1000,
    #         output_cdf_prefix=output_cdf_prefix + outputprefix,
    #         commentLines=commentLines,
    #         )
    #     pass

    return

#############################################
if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawTextHelpFormatter,
        description='''
process the result of shadow simulations.

first, use the "parse" command to parse a simulation\'s logs into a pickle file. then use "analyze" command on the pickle files.
        '''
        )

    parser.add_argument(
        "--log-level", dest='log_level', default='INFO',
        help="set log level")

    subparsers = parser.add_subparsers(help='sub-command help',
                                       dest='subparser_name')

    pickleparser = subparsers.add_parser(
        'parse', help='parse help')
    pickleparser.add_argument(
        "-d", "--description", default='', dest='description',
        help="optional description for this experiment/result. to be stored in picked file")
    pickleparser.add_argument(
        "-f", "--force",
        action="store_true", dest="force", default=False,
        help="force parse in cases that parse() would stop prematurely, e.g., when a pickled file already exists")
    pickleparser.add_argument(
        "expdir",
        help="path to shadow experiment dir, that contains \"shadow.log\" and \"shadow.data\"")

    analyzeparser = subparsers.add_parser(
        'analyze', help='analyze results in parsed, pickled files')
    analyzeparser.add_argument(
        "pickledfiles", metavar='file', nargs='+',
        help="path(s) to pickled result file(s) (e.g., parsedPickledResults-v%u.bz2)" % (g_version))
    analyzeparser.add_argument(
        "--show-info-only",
        action="store_true", dest="show_info_only", default=False,
        help="only show the descriptions and meta info from the pickle file(s)")
    analyzeparser.add_argument(
        "--web-only",
        action="store_true", dest="web_only", default=False,
        help="process only web results, i.e., skip bulk and torperf results")
    analyzeparser.add_argument(
        "--web-url", action='append', metavar='web_url', dest='web_urls',
        help="only consider these web url(s)")
    analyzeparser.add_argument(
        "--web-ttfb-bucketsize", type=float, default=0.5,
        help="cdf bucket size for time to first byte for web page loads")
    analyzeparser.add_argument(
        "--bulk-ttfb-bucketsize", type=float, default=0.5,
        help="cdf bucket size for time to first byte for bulk downloads")
    analyzeparser.add_argument(
        "--web-plt-bucketsize", type=float, default=5,
        help="cdf bucket size for web page load time")
    analyzeparser.add_argument(
        "--bulk-fdt-bucketsize", type=float, default=5,
        help="cdf bucket size for bulk download time")
    analyzeparser.add_argument(
        "--min-num-objects", type=int,
        help="only use pages with at least this many objects")
    analyzeparser.add_argument(
        "--output-cdf-prefix", default='',
        help="prefix for output cdf files")

    analyzeparser.add_argument(
        "--count-downloads-and-bytes",
        action="store_true", dest="count_bytes_only", default=False,
        help="only count downloads and transferred bytes. don't generate plt and ttfb CDFs.")

    analyzeparser.add_argument(
        "--startAfter",
        help="only use load results that start at/after this virtual time in hr:min:sec format, e.g., 1:15:0",
        default="00:30:00")
    analyzeparser.add_argument(
        "--doneBefore",
        help="only use load results that completes at/before this virtual time in hr:min:sec format, e.g., 1:15:0")

    args = parser.parse_args()

    # assuming loglevel is bound to the string value obtained from the
    # command line argument. Convert to upper case to allow the user to
    # specify --log=DEBUG or --log=debug
    numeric_level = getattr(logging, args.log_level.upper(), None)
    if not isinstance(numeric_level, int):
        raise ValueError('Invalid log level: %s' % args.log_level)
    rootLogger = logging.getLogger()
    assert not rootLogger.hasHandlers()

    my_handler = logging.StreamHandler()
    my_formatter = logging.Formatter(
        fmt="%(levelname) -10s %(asctime)s %(module)s:%(lineno)d, %(funcName)s(): %(message)s",
        datefmt='%I:%M:%S')
    my_handler.setFormatter(my_formatter)
    rootLogger.addHandler(my_handler)
    assert rootLogger.hasHandlers()
    rootLogger.setLevel(numeric_level)

    if args.subparser_name == 'parse':
        parse(args.expdir, description=args.description,
              force=args.force)
        pass
    elif args.subparser_name == 'analyze':
        if args.web_urls:
            web_urls = set(args.web_urls)
            pass
        else:
            web_urls = set()
            pass

        startAfter = convert_to_second(args.startAfter)

        if args.doneBefore:
            doneBefore = convert_to_second(args.doneBefore) 
            if startAfter > doneBefore:
                print( 'ERROR: startAfter is greater than doneBefore')
                sys.exit(1)
                pass
            pass
        else:
            doneBefore = None
            pass

        if args.count_bytes_only:
            count_downloads_and_bytes(
                filepaths=args.pickledfiles,
                startAfter=startAfter,
                doneBefore=doneBefore,
                )
            pass
        else:
            analyze(filepaths=args.pickledfiles,
                    show_info_only=args.show_info_only,
                    web_only=args.web_only,
                    web_urls=web_urls,
                    web_plt_bucketsize=args.web_plt_bucketsize,
                    web_ttfb_bucketsize=args.web_ttfb_bucketsize,
                    bulk_fdt_bucketsize=args.bulk_fdt_bucketsize,
                    bulk_ttfb_bucketsize=args.bulk_ttfb_bucketsize,
                    min_num_objects=args.min_num_objects,
                    output_cdf_prefix=args.output_cdf_prefix,
                    startAfter=startAfter,
                    doneBefore=doneBefore,
                    commentLines=[' '.join(sys.argv)],
                    )
            pass
        pass

    else:
        parser.print_help()
        pass

    pass
