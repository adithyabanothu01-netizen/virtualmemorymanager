
FROM gcc:13

WORKDIR /app

COPY virtual.cpp .

RUN g++ -std=c++11 -O2 virtual.cpp -o vmm_server

ENV PORT=8080

EXPOSE 8080

CMD ["sh", "-c", "./vmm_server"]
