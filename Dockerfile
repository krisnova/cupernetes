FROM debian:latest
RUN apt-get update
RUN apt-get install -y cups
COPY ./etc/error_log /var/log/cups/error_log
COPY ./etc/cupsd.conf /etc/cups/cupsd.conf
COPY ./etc/cupernetes /usr/bin/cupernetes
ENTRYPOINT ["cupernetes"]