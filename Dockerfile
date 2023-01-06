FROM alpine

RUN apk add --no-cache \
      bash \
      gcompat \
      libstdc++ \
      x11vnc \
      xcb-util-wm \
      xclock \
      xeyes \
      xprop \
      xvfb

RUN mkdir ~/.vnc \
 && x11vnc -storepasswd secret ~/.vnc/passwd \
 && echo -e "#!/bin/bash\nx11vnc -forever -usepw -create" > /run.sh \
 && echo -e "#!/bin/bash\nwhile true; do sleep 100; done" > ~/.xinitrc \
 && chmod +x /run.sh ~/.xinitrc

EXPOSE 5900

CMD ["/run.sh"]
