.. _Creating_Config:

================================================
Creating a Configuration File
================================================

The configuration file is a central fixture of the ChampSim build system.
An example configuration file (champsim_config.json) is given in the root of the repository, but it is large and unwieldy.
Your configuration file will likely be much smaller.
This page will walk you through many of the features of the ChampSim configuration system.

The configuration file is given as an input to the configuration script as below::

    ./config.sh my_config.json

In fact, the configuration file is entirely optional.
Not specifying any file will configure ChampSim with a default configuration.

-------------------------------
Your first configuration file
-------------------------------

All ChampSim configuration files are JSON files.
We can start with the most trivial of configuration files.::

    {
    }

This would specify a default configuration.
But, this is not frequently useful.
Let's change the branch predictor that ChampSim uses.
Legal values for the ``branch_predictor`` key are directory names under the ``branch/`` directory, or valid paths.
The same is true for specifying BTBs in the core and both prefetchers and replacement policies in the cache.::

    {
        "branch_predictor": "perceptron"
    }

This configuration file will configure ChampSim with a default configuration, but with a perceptron branch predictor instead.
We can take this further and specify many things about our single-core system.::

    {
        "branch_predictor": "perceptron",

        "rob_size": 226, "lq_size": 90, "sq_size": 85,
        "fetch_width": 6, "decode_width": 6, "lq_width": 2, "sq_width": 1,

        "decode_latency": 3, "execute_latency": 2
    }

Each of these options will specify something about our core. Next, we'll specify some of our caches.

---------------------
Cache Configuration
---------------------

We can begin by changing some features of our L1 data cache.
We can specify its size based on the block size, the number of sets, and the number of ways.
The following configuration file specifies a 64 KiB L1 data cache.::

    {
        "block_size": 64,
        "L1D": { "sets": 256, "ways": 4 }
    }

Note that 64 is the default block size, specified here for clarity.
The default prefetcher is the do-nothing prefetcher, and the default replacement policy is LRU, but they can be specified, too.::

    {
        "L1D": {
            "sets": 256, "ways": 4,
            "prefetcher": "next_line",
            "replacement": "../../my/repl/path"
        }
    }

Specifying a cache this way will create an identical L1D for each core in the configuration.
So far, we've only handled the single-core case.

--------------------------
Cache inclusion policy
--------------------------

Each cache has an inclusion policy that describes its relationship to the caches above it.
Three policies are supported:

``"non-inclusive"`` (default)
    The contents of the cache are independent of the upper levels. This is ChampSim's historical behavior
    (often called NINE, non-inclusive non-exclusive). Evictions do not affect the upper levels, and the
    replacement policy may bypass fills.

``"inclusive"``
    Every block held by an upper-level cache is also held by this cache. When the cache evicts a block,
    every copy in the levels above it (transitively, e.g. L2C *and* L1D for an inclusive LLC) is
    back-invalidated. If any of those copies was dirty, the eviction is written back to the lower level even
    when this cache's copy was clean. Fills into an inclusive cache may not bypass; replacement policies can
    check ``CACHE::bypass_allowed()``.

``"exclusive"``
    A block is held either by this cache or by the caches above it, never both. The cache acts as a victim cache:
    misses from an upper-level cache are passed up without allocating here, a demand hit from an upper-level cache
    moves the block up (invalidating it here, and handing over ownership if it was dirty), and the upper-level caches
    send every eviction here, clean or dirty. Clean victims are not written to memory. Requests that do not come
    from an upper-level cache (for example, from a core) are handled as in a non-inclusive cache, so marking an L1
    cache exclusive has no effect.

    Because every fill into an exclusive cache is an upper-level eviction, it arrives as a ``WRITE`` access with no
    instruction pointer. Replacement policies that treat writebacks as dead or that train on the PC (for example,
    SHiP and Mockingjay) will behave very differently in an exclusive cache. In the statistics, these fills appear
    as ``WRITE`` misses, so compute demand MPKI from the ``LOAD`` and ``RFO`` rows rather than ``TOTAL``.

The policy can be set for the whole data-cache hierarchy with a top-level key::

    {
        "inclusion_policy": "inclusive"
    }

or for individual caches, which overrides the top-level value::

    {
        "inclusion_policy": "inclusive",
        "L2C": { "inclusion_policy": "non-inclusive" }
    }

The top-level key applies only to the data caches (L1I, L1D, L2C, LLC and any caches below them). TLBs remain
non-inclusive unless a TLB sets ``"inclusion_policy"`` itself. ``"non_inclusive"`` and ``"nine"`` are accepted
as synonyms for ``"non-inclusive"``; any other value is an error.

Caches that perform back-invalidations report them in the statistics::

    LLC BACK-INVALIDATIONS ISSUED:      12345 RECEIVED:          0 DIRTY WRITEBACKS:        678
    cpu0_L2C BACK-INVALIDATIONS ISSUED:   2345 RECEIVED:      12000 DIRTY WRITEBACKS:         12

``ISSUED`` counts upper-level blocks invalidated by this cache's evictions, ``RECEIVED`` counts blocks in this
cache invalidated by a lower inclusive cache, and ``DIRTY WRITEBACKS`` counts evictions that were written back
only because an upper copy was dirty.

Exclusive caches report::

    LLC EXCLUSIVE PROMOTIONS:     464545 FILLS SKIPPED:     519154 CLEAN VICTIMS RECEIVED:     592511

``PROMOTIONS`` counts hits that moved a block up, ``FILLS SKIPPED`` counts misses that were passed up without
allocating, and ``CLEAN VICTIMS RECEIVED`` counts clean upper-level evictions inserted into the cache.

Policies can be mixed across levels, for example an inclusive L2C above an exclusive LLC::

    {
        "L2C": { "inclusion_policy": "inclusive" },
        "LLC": { "inclusion_policy": "exclusive" }
    }

--------------------------
Multi-core configurations
--------------------------

Multi-core simulations can be enabled by specifying the ``num_cores`` key.::

    {
        "num_cores": 2
    }

This will create two identical cores with default configurations.
This can be combined with the other specifications that we have made so far to create identical cores.::

    {
        "num_cores": 2,
        "branch_predictor": "perceptron",

        "rob_size": 190,

        "L1D": {
            "sets": 256, "ways": 4,
            "prefetcher": "next_line",
            "replacement": "drrip"
        }
    }

-----------------------
Heterogeneous systems
-----------------------

ChampSim supports a variety of system configurations, including systems that are not homogeneous.
For example, we could create two cores with different ROB sizes.
Specify all cores in a list under the ``ooo_cpu`` key.::

    {
        "num_cores": 2,
        "ooo_cpu": [
            { "rob_size": 120 },
            { "rob_size": 240 }
        ]
    }

Each object in the ``ooo_cpu`` list specifies one core, and takes all of the options that we have discussed so far.::


    {
        "num_cores": 2,
        "ooo_cpu": [
            {
                "branch_predictor": "bimodal",
                "rob_size": 120, "lq_size": 90,
                "L1D": { "prefetcher": "next_line" }
            },
            {
                "branch_predictor": "gshare",
                "rob_size": 240, "lq_size": 70,
                "L1D": { "prefetcher": "no" }
            }
        ]
    }

Each cache object can also be specified in a list under the ``caches`` key.
These caches can then be referred to by their ``name`` key.
In the following configuration, each core has a distinct L1 cache.::

    {
        "num_cores": 2,
        "ooo_cpu": [
            { "L1D": "cacheA" },
            { "L1D": "cacheB" }
        ],
        "caches": [
            { "name": "cacheA", "replacement": "lru" },
            { "name": "cacheB", "replacement": "srrip" }
        ]
    }

The configuration script will make every attempt to assign defaults to objects, but it may not be able to do so for.
In the following configuration, cores 0 and 1 are attached to ``llcA`` and cores 2 and 3 are attached to ``llcB``.
The script is able to assign LLC-like defaults to each of the caches specified under ``"caches"``::

    {
        "num_cores": 4,
        "ooo_cpu": [
            { "L2C": { "lower_level": "llcA"} },
            { "L2C": { "lower_level": "llcB"} }
        ],
        "caches": [
            { "name": "llcA" },
            { "name": "llcB" }
        ]
    }

However, in the following, the script will not be able to assign defaults to all caches, and you may need to specify additional parameters::

    {
        "num_cores": 8,
        "ooo_cpu": [
            { "L2C": { "lower_level": "llcA"} },
            { "L2C": { "lower_level": "llcB"} }
        ],
        "caches": [
            { "name": "llcA", "lower_level": "L4C" },
            { "name": "llcB", "lower_level": "L4C" },
            { "name": "L4C" }
        ]
    }
