FROM node:22-alpine
WORKDIR /app
COPY server/package.json server/
COPY server/src server/src
COPY Game/Content/Data/Story Game/Content/Data/Story
WORKDIR /app/server
ENV PORT=8080 DATA_DIR=/data
VOLUME /data
EXPOSE 8080
CMD ["node", "--disable-warning=ExperimentalWarning", "src/index.js"]
