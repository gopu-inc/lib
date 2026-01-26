import bcrypt

# Mot de passe en bytes
password = "Mauricio-100".encode('utf-8')

# Générer le hash bcrypt
hashed = bcrypt.hashpw(password, bcrypt.gensalt())

# Afficher le hash
print(hashed.decode('utf-8'))
