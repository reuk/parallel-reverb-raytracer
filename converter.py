import json

def main():
    with open('mat.json') as mat_file:
        obj = json.load(mat_file)

    for i in obj:
        obj[i]['specular'] = [1 - j for j in obj[i]['specular']]

    with open('mat_updated.json', 'w') as mat_file:
        json.dump(obj, mat_file)

if __name__ == '__main__':
    main()
