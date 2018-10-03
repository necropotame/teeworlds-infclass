while true;
do
	cp infclassr.log "logs/infclassr-$(date +"%d-%m-%y-%r").log"
	./server_sql
done
