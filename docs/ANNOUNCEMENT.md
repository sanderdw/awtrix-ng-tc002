# Official AWTRIX support for the TC002 and the future of this port

I started this project because I wanted to run AWTRIX on my TC002, just as I already did on my TC001. At the time, there was no AWTRIX firmware for it, and official support had been ruled out. On 28 August, Blueforcer answered [“No” in Discord](https://discord.com/channels/546407049148366859/1542847001564024843). He [confirmed that decision on GitHub on 1 September](https://github.com/Blueforcer/awtrix-ng/discussions/50#discussioncomment-18230536) and [explained on 7 September](https://github.com/Blueforcer/awtrix-ng/discussions/50#discussioncomment-18331029) that the different architecture and closed Linux platform would require a complete rewrite that he did not consider worthwhile. Other TC002 owners were already [returning their clocks](https://github.com/Blueforcer/awtrix-ng/discussions/50#discussioncomment-18331113) or [considering doing so](https://github.com/Blueforcer/awtrix-ng/discussions/50#discussioncomment-18444626). With no official version planned, I decided to build a port myself. I focused on preserving AWTRIX NG’s existing functionality, adding only a dedicated MQTT topic for the knob for use with Home Assistant discovery.

On 15 September, I [published the first version at 13:33](https://github.com/sanderdw/awtrix-ng-tc002/commit/7661dac) and messaged Blueforcer at 13:42 to introduce it and ask for his thoughts. At 14:22 that same day, he [announced that he would build official TC002 firmware](https://discord.com/channels/546407049148366859/591248067735322624/1549395086708842606).

I am happy he has decided to support the TC002 himself, including the underlying Linux system and firmware. His plans for a maintained system image, authenticated access, HTTPS, signed updates and reliable recovery go beyond the application work in this port. I think this project helped demonstrate interest in AWTRIX on the TC002 and contributed to that change of direction. I am glad to support an official version maintained by the original AWTRIX developer.

Also keep an eye on [Stipple](https://galadril.github.io/Stipple/), another project that looks really interesting. I hope to see more projects making use of the TC002’s hardware and opening it up for the community.

Ulanzi has also featured this project on its [TC002 product page](announcement/ulanzi-productpage.png) and in its [newsletter](announcement/ulanzi-newsletter.png). I have never had any contact with Ulanzi, and it came as a surprise to see the project featured there. I was happy to see the project recognized and shared with other TC002 owners.

**If you already use this port, I will keep supporting it until Blueforcer releases the official TC002 version.** Please continue to use this repository’s [issues](https://github.com/sanderdw/awtrix-ng-tc002/issues) for questions and bug reports about the port. Once the official version is available, I will point users to it.

When you are ready to switch, you can first restore the stock Ulanzi application. Run the following command on the computer you used to install this port, connected to the same network as your clock:

```sh
curl -fsSL https://raw.githubusercontent.com/sanderdw/awtrix-ng-tc002/main/install.sh | sh -s -- CLOCK_IP --restore
```

See the [installation guide](INSTALL.md#going-back-to-stock) for backup requirements, recovery steps and switching to the official firmware.

This port exists thanks to Blueforcer and the AWTRIX community’s work. Thank you to everyone who has tested it, reported issues and helped improve it. Please [support Blueforcer](https://ko-fi.com/blueforcer) as he brings official AWTRIX support to the TC002.
