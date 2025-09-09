import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS

# Informações de acesso para o InfluxDB
# O nome do serviço ser 'influxdb', ao invés de 'localhost' permite que o servidor se comunique com o container do InfluxDB criado automaticamente pelo Docker
url = "http://influxdb:8086"
token = "xuWbg9tlOvm2OzEQWj_C-SLHqGTs-u7dpfMyHF8A3iTmy5KYc9Q-B0JFmq4WE-D_RomKXv9xlhYOsjJ8eEZxew==" # -> token influx 
org = "Projeto Ambev"
bucket = "dados_sensores"

# Conecte-se ao cliente InfluxDB
client = influxdb_client.InfluxDBClient(url=url, token=token, org=org)
write_api = client.write_api(write_options=SYNCHRONOUS)

# Ponto de dados para a leitura do sensor de cor
# TODO | (Junior) Substitua os valores de exemplo por variáveis do teu código que recebem os dados reais do sensor.
valor_vermelho = 100   
valor_verde = 150      
valor_azul = 200       

ponto_de_dados = influxdb_client.Point("sensor_cor") \
    .tag("localizacao", "elevatorio") \
    .field("vermelho", valor_vermelho) \
    .field("verde", valor_verde) \
    .field("azul", valor_azul)

# Envia o ponto de dados para o InfluxDB
write_api.write(bucket=bucket, org=org, record=ponto_de_dados)

# Fecha a conexão
client.close()

print("Dados do sensor de cor enviados com sucesso!")