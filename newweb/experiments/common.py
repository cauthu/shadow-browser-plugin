import math

def convert_to_second(s):
    '''convert "hr:min:sec" to seconds, a floating point. <sec> can be floating point
    '''
    parts = s.split(':')
    assert (len(parts) == 3), 'must be in hr:min:sec format, where <sec> can be floating point'
    val = (int(parts[0])*3600) + (int(parts[1])*60) + (float(parts[2]))
    assert val > 0
    return val

# def convert_to_ms(s):
#     parts = map(int, s.split(':'))
#     assert (len(parts) == 3), 'must be in hr:min:sec format'
#     second = (parts[0]*3600) + (parts[1]*60) + (parts[2]) # in second
#     return second * 1000

def convert_ms_to_str(ms):
    hour = int(ms) / (3600 * 1000)
    ms = ms % (3600 * 1000)
    minute = int(ms) / (60 * 1000)
    ms = ms % (60 * 1000)
    second = int(ms) / (1000)
    ms = ms % (1000)
    return '{hour:02d}:{minute:02d}:{second:02d}:{ms:03d}'.format(
        hour=hour, minute=minute, second=second, ms=ms)

# http://stackoverflow.com/questions/1094841/reusable-library-to-get-human-readable-version-of-file-size
def sizeof_fmt(num):
    for x in ['bytes','KB','MB','GB','TB']:
        if num < 1024.0:
            return "%3.1f %s" % (num, x)
        pass
        num /= 1024.0
        pass
    return

def genCDF(sortedList, toFilepath=None,
           bucketsize=0,
           commentLines=[],
           ):
    retval = None
    length = len(sortedList)
    if toFilepath:
        fil = open(toFilepath, 'w')
        pass
    else:
        retval = []
        pass
    ###

    for line in commentLines:
        fil.write('# ' + line + '\n')
        pass

    if bucketsize > 0:
        # this should be able to handle negative values as well.  (-0
        # == 0) is True

        # each bucket is represented by its upper bound.  for example,
        # if bucketsize is 10, then buckets are (-10, 0], (0, 10],
        # (10, 20], ... and the representatives of the groups are 0,
        # 10, and 20 respectively.

        # bucketUpperBound -> count
        bucketCountDict = {}

        # NOTE: we want the smallest value to get its own bucket if
        # applicable
        minvalue = min(sortedList)
        bucketCountDict[minvalue] = 0

        # this loop is not exploiting the fact that the sortedList
        # list is already sorted, but this is simpler.
        for val in sortedList:
            if val == minvalue:
                bucketCountDict[minvalue] += 1
                continue
            # which bucket should this go into?
            bucketUpperBound = math.ceil((float(val)) / bucketsize) * bucketsize
            if bucketUpperBound in bucketCountDict:
                # bucket exists already -> increment its count
                bucketCountDict[bucketUpperBound] += 1
                pass
            else:
                # create the bucket
                bucketCountDict[bucketUpperBound] = 1
                pass
            pass

#        print '***** buckets and their counts: ', bucketCountDict

        bucketUpperBounds = sorted(bucketCountDict.keys())
        # bucketUpperBounds.sort()

        cumulativeCount = 0
        for bucketUpperBound in bucketUpperBounds:
            cumulativeCount += bucketCountDict[bucketUpperBound]
            fraction = float(cumulativeCount) / len(sortedList)
            if toFilepath:
                fil.write('%f    %f\n' % (bucketUpperBound, fraction))
                pass
            else:
                retval.append((bucketUpperBound, fraction))
                pass
            pass
        pass
    else:
        for idx, val in enumerate(sortedList):
            if toFilepath:
                fil.write('%f    %f\n' % (val, (idx + 1.0) / length))
                pass
            else:
                retval.append((val, (idx + 1.0) / length))
                pass
            pass
        pass
    ###

    if toFilepath:
        fil.close()
        pass
    return retval
