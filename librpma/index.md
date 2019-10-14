---
title: librpma
layout: pmdk
---

#### The librpma library

**librpma** provides low-level support for remote access to
*persistent memory* (pmem) utilizing RDMA-capable RNICs. The library can be
used to remotely read and write a memory region over the RDMA protocol. It utilizes
an appropriate persistency mechanism based on the remote node's platform
capabilities.

This library is for applications that use remote persistent memory directly
to preserve complete control over data transmission process.

Man pages that contains a list of the **Linux** interfaces provided:

* Man page for <a href="../manpages/linux/master/librpma/{{ page.title }}.7.html">{{ page.title }} current master</a>

#### The rpmactl utility

The **rpmactl** allows XXX

See the [rpmactl man page](../manpages/linux/master/rpmactl/rpmactl.1.html)
for current master documentation and examples or see older version:

<ul>
   {% assign command = 'rpmactl' %}
   {% for release in site.data.releases %}{% if release.libs contains command %}
   <li><a href="../manpages/linux/v{{ release.tag }}/{{ command }}.1.html">{{ command }} version {{ release.tag }}</a></li>
   {% endif %}{% endfor %}
</ul>

#### librpma Examples XXX

**More Detail Coming Soon**

<code data-gist-id='janekmi/47fa646ada72b27f77692672e9d7988e' data-gist-file='manpage.c' data-gist-line='36-96' data-gist-highlight-line='42' data-gist-hide-footer='true'></code>
