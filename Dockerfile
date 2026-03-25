FROM ps2dev/ps2dev:latest
RUN apk add --no-cache make gmp mpfr4 mpc1
WORKDIR /src
ENTRYPOINT ["make"]
