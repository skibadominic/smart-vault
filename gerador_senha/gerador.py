import hashlib
from Crypto.Cipher import AES

SEGREDO_MESTRE = b"CHAVE_MESTRA_COFRE_PUCPR_2026_X9"
PAYLOAD_VALIDO = b"COFRE_AUTH_OK_TOKEN_VERIFICADO!"

IV_FIXO = bytes([
    0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18,
    0x29, 0x3A, 0x4B, 0x5C, 0x6D, 0x7E, 0x8F, 0x90
])

chave_aes = hashlib.sha256(SEGREDO_MESTRE).digest()
bloco_plano = PAYLOAD_VALIDO.ljust(32, b"\x00")

cifra = AES.new(chave_aes, AES.MODE_CBC, IV_FIXO)
bloco_cifrado = cifra.encrypt(bloco_plano)

with open("token.bin", "wb") as arquivo:
    arquivo.write(bloco_cifrado)

print(f"Arquivo token.bin ({len(bloco_cifrado)} bytes) gerado com sucesso!")
print("Hexadecimal gerado:", bloco_cifrado.hex().upper())